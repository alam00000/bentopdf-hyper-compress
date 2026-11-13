use std::collections::VecDeque;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::Duration;

use hyper_compress::{compress, verify_password, Binaries, CompressInput, ErrorCode, Preset};
use serde_json::{json, Map, Value};
use tiny_http::{Header, Request, Response, Server};

fn int_env(name: &str, dflt: u64, min: u64, max: u64) -> u64 {
    match std::env::var(name).ok().map(|v| v.trim().to_string()).filter(|v| !v.is_empty()) {
        None => dflt,
        Some(v) => v.parse::<f64>().ok().filter(|n| n.is_finite()).map(|n| (n.floor().max(0.0) as u64).clamp(min, max)).unwrap_or(dflt),
    }
}

struct Config {
    max_upload: u64,
    concurrency: usize,
    queue_limit: usize,
    timeout: Duration,
    token: String,
    root: Option<PathBuf>,
    index: Vec<u8>,
}

#[derive(Default)]
struct Gate {
    active: usize,
    waiting: VecDeque<u64>,
    next: u64,
}

struct Slots {
    gate: Mutex<Gate>,
    cv: Condvar,
}

struct Slot<'a>(&'a Slots);

impl Drop for Slot<'_> {
    fn drop(&mut self) {
        let mut g = self.0.gate.lock().unwrap();
        g.active -= 1;
        self.0.cv.notify_all();
    }
}

impl Slots {
    fn acquire(&self, cfg: &Config) -> Option<Slot<'_>> {
        let mut g = self.gate.lock().unwrap();
        if g.active < cfg.concurrency && g.waiting.is_empty() {
            g.active += 1;
            return Some(Slot(self));
        }
        if g.waiting.len() >= cfg.queue_limit {
            return None;
        }
        let ticket = g.next;
        g.next += 1;
        g.waiting.push_back(ticket);
        while !(g.active < cfg.concurrency && g.waiting.front() == Some(&ticket)) {
            g = self.cv.wait(g).unwrap();
        }
        g.waiting.pop_front();
        g.active += 1;
        self.cv.notify_all();
        Some(Slot(self))
    }

    fn counts(&self) -> (usize, usize) {
        let g = self.gate.lock().unwrap();
        (g.active, g.waiting.len())
    }
}

fn header(name: &str, value: &str) -> Header {
    Header::from_bytes(name.as_bytes(), value.as_bytes()).unwrap()
}

fn send_json(req: Request, status: u16, body: Value) {
    let _ = req.respond(Response::from_string(body.to_string()).with_status_code(status).with_header(header("content-type", "application/json")));
}

fn send_busy(req: Request) {
    let body = json!({ "error": "busy", "detail": "queue full, retry later" }).to_string();
    let _ = req.respond(
        Response::from_string(body)
            .with_status_code(429)
            .with_header(header("content-type", "application/json"))
            .with_header(header("retry-after", "1")),
    );
}

fn get_header(req: &Request, name: &str) -> Option<String> {
    req.headers().iter().find(|h| h.field.as_str().as_str().eq_ignore_ascii_case(name)).map(|h| h.value.as_str().to_string())
}

fn password(req: &Request) -> Option<String> {
    get_header(req, "x-password-encoded").map(|v| decode_component(&v)).or_else(|| get_header(req, "x-password"))
}

fn authorized(req: &Request, cfg: &Config) -> bool {
    if cfg.token.is_empty() {
        return true;
    }
    let provided = match get_header(req, "authorization") {
        Some(h) if h.starts_with("Bearer ") => h[7..].to_string(),
        _ => get_header(req, "x-api-token").unwrap_or_default(),
    };
    let (a, b) = (provided.as_bytes(), cfg.token.as_bytes());
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

fn read_body(req: &mut Request, limit: u64) -> Option<Vec<u8>> {
    let mut body = Vec::new();
    let n = req.as_reader().take(limit + 1).read_to_end(&mut body).ok()?;
    (n as u64 <= limit).then_some(body)
}

fn query(url: &str) -> Map<String, Value> {
    let mut m = Map::new();
    if let Some(q) = url.split_once('?').map(|(_, q)| q) {
        for pair in q.split('&').filter(|p| !p.is_empty()) {
            let (k, v) = pair.split_once('=').unwrap_or((pair, ""));
            m.entry(decode(k)).or_insert(Value::String(decode(v)));
        }
    }
    m
}

fn decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < bytes.len() => {
                match std::str::from_utf8(&bytes[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok()).ok_or(()) {
                    Ok(b) => {
                        out.push(b);
                        i += 2;
                    }
                    Err(_) => out.push(b'%'),
                }
            }
            b => out.push(b),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn decode_component(s: &str) -> String {
    decode(&s.replace('+', "%2B"))
}

fn encode_component(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.!~*'()".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

fn handle_compress(mut req: Request, cfg: &Config, slots: &Slots) {
    let q = query(req.url());
    let param = |k: &str| q.get(k).and_then(Value::as_str).map(str::to_string);
    let preset_raw = param("preset").unwrap_or_else(|| "medium".into());
    let custom = preset_raw == "custom";
    let preset = Preset::parse(&preset_raw);
    if !custom && preset.is_none() {
        return send_json(req, 400, json!({ "error": "bad_preset", "presets": ["low", "medium", "high", "lossless", "custom"] }));
    }
    let target = match param("targetSizeBytes").filter(|s| !s.is_empty()) {
        None => None,
        Some(t) => match t.trim().parse::<f64>().ok().filter(|n| n.is_finite() && *n > 0.0) {
            Some(n) => Some(n.floor() as u64),
            None => return send_json(req, 400, json!({ "error": "bad_target_size" })),
        },
    };
    let mut overrides: Option<Map<String, Value>> = None;
    if let Some(raw) = param("options").filter(|s| !s.is_empty()) {
        if raw.encode_utf16().count() > 4096 {
            return send_json(req, 400, json!({ "error": "bad_options" }));
        }
        match serde_json::from_str::<Value>(&raw) {
            Ok(Value::Object(m)) => overrides = Some(m),
            _ => return send_json(req, 400, json!({ "error": "bad_options" })),
        }
    }
    if custom && overrides.is_none() {
        return send_json(req, 400, json!({ "error": "options_required" }));
    }
    let mut options = overrides.clone().unwrap_or_default();
    if param("brotli").as_deref() == Some("true") {
        options.insert("brotli".into(), Value::Bool(true));
    }
    if param("preservePdfa").as_deref() == Some("true") {
        options.insert("preserveConformance".into(), Value::Bool(true));
    }
    let password = password(&req);

    let Some(_slot) = slots.acquire(cfg) else {
        return send_busy(req);
    };
    let Ok(dir) = tempfile::Builder::new().prefix("hyperd-").tempdir() else {
        return send_json(req, 500, json!({ "error": "internal" }));
    };
    let Some(body) = read_body(&mut req, cfg.max_upload) else {
        return send_json(req, 413, json!({ "error": "too_large", "maxBytes": cfg.max_upload }));
    };
    if body.len() < 5 || &body[..4] != b"%PDF" {
        return send_json(req, 400, json!({ "error": "not_a_pdf" }));
    }
    let src = dir.path().join("in.pdf");
    let dst = dir.path().join("out.pdf");
    if fs::write(&src, &body).is_err() {
        return send_json(req, 500, json!({ "error": "internal" }));
    }
    drop(body);
    let input = CompressInput {
        source_path: src,
        save_path: Some(dst),
        password,
        preset: if custom { None } else { preset },
        options,
        target_size_bytes: target,
        stage_timeout: Some(cfg.timeout),
        total_timeout: Some(cfg.timeout),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    match compress(&input) {
        Ok(r) => {
            let Ok(out) = fs::read(&r.output_path) else { return send_json(req, 500, json!({ "error": "internal" })) };
            let pdfa = r.pdfa.as_ref().map(|l| format!("{}{}", l.part, l.conformance)).unwrap_or_default();
            let met = r.met_target.map(|m| m.to_string()).unwrap_or_default();
            let warnings = if r.warnings.is_empty() { String::new() } else { encode_component(&Value::from(r.warnings.clone()).to_string()) };
            let resp = Response::from_data(out)
                .with_header(header("content-type", "application/pdf"))
                .with_header(header("x-original-size", &r.original_size.to_string()))
                .with_header(header("x-compressed-size", &r.compressed_size.to_string()))
                .with_header(header("x-signed", &r.signed.to_string()))
                .with_header(header("x-signatures-removed", &r.signatures_removed.to_string()))
                .with_header(header("x-pdfa", &pdfa))
                .with_header(header("x-pdfa-preserved", &r.pdfa_preserved.to_string()))
                .with_header(header("x-met-target", &met))
                .with_header(header("x-warnings", &warnings));
            let _ = req.respond(resp);
        }
        Err(e) => {
            let status = match e.code {
                ErrorCode::DecryptFailed => 400,
                ErrorCode::Timeout => 504,
                _ => 500,
            };
            send_json(req, status, json!({ "error": e.code.as_str() }));
        }
    }
}

fn handle_verify(mut req: Request, cfg: &Config, slots: &Slots) {
    let password = password(&req).unwrap_or_default();
    let Some(_slot) = slots.acquire(cfg) else {
        return send_busy(req);
    };
    let Ok(dir) = tempfile::Builder::new().prefix("hyperv-").tempdir() else {
        return send_json(req, 500, json!({ "error": "internal" }));
    };
    let Some(body) = read_body(&mut req, cfg.max_upload) else {
        return send_json(req, 413, json!({ "error": "too_large", "maxBytes": cfg.max_upload }));
    };
    let src = dir.path().join("in.pdf");
    if fs::write(&src, &body).is_err() {
        return send_json(req, 500, json!({ "error": "internal" }));
    }
    match verify_password(&src, &password, None, &Binaries::beside_exe()) {
        Ok(valid) => send_json(req, 200, json!({ "valid": valid })),
        Err(_) => send_json(req, 500, json!({ "error": "internal" })),
    }
}

fn static_file(req: Request, cfg: &Config, url_path: &str) -> Option<Request> {
    let ok_path = url_path.len() > 1
        && (url_path.starts_with("/dist/") || url_path.starts_with("/web/"))
        && [".js", ".css", ".woff2", ".svg", ".png"].iter().any(|e| url_path.ends_with(e))
        && url_path[..url_path.rfind('.').unwrap_or(0)].chars().skip(1).all(|c| c.is_ascii_alphanumeric() || "/_-".contains(c))
        && !url_path.contains("..");
    let Some(root) = cfg.root.as_ref().filter(|_| ok_path) else { return Some(req) };
    let target = root.join(&url_path[1..]);
    match fs::read(&target) {
        Ok(buf) => {
            let kind = if url_path.ends_with(".svg") {
                "image/svg+xml"
            } else if url_path.ends_with(".png") {
                "image/png"
            } else if url_path.ends_with(".css") {
                "text/css; charset=utf-8"
            } else if url_path.ends_with(".woff2") {
                "font/woff2"
            } else {
                "text/javascript; charset=utf-8"
            };
            let _ = req.respond(Response::from_data(buf).with_header(header("content-type", kind)));
        }
        Err(_) => send_json(req, 404, json!({ "error": "not_found" })),
    }
    None
}

fn find_root() -> Option<PathBuf> {
    if let Some(r) = std::env::var_os("HYPER_ROOT") {
        return Some(PathBuf::from(r));
    }
    let exe = std::env::current_exe().ok()?;
    exe.ancestors().take(8).find(|d| d.join("web").join("index.html").is_file()).map(Path::to_path_buf)
}

fn index_page(root: &Option<PathBuf>) -> Vec<u8> {
    let fallback = b"<h1>Hyper Compress</h1><p>POST a PDF to /api/compress</p>".to_vec();
    let Some(page) = root.as_ref().and_then(|r| fs::read_to_string(r.join("web").join("index.html")).ok()) else { return fallback };
    let page = page
        .replace("href=\"./app.css\"", "href=\"/web/app.css\"")
        .replace("<div id=\"status\">Loading engine...</div>", "<div id=\"status\"></div>")
        .replace("import { initApp } from './app.js';", "import { initApp } from '/web/app.js';")
        .replace("import { createWasmEngine } from './engine-wasm.js';", "import { createHttpEngine } from '/web/engine-http.js';")
        .replace("initApp(createWasmEngine);", "initApp(createHttpEngine);")
        .replace("\"./images/", "\"/web/images/");
    if !page.contains("engine-http.js") || page.contains("createWasmEngine") {
        eprintln!("web/index.html changed shape; update the server transform");
        std::process::exit(1);
    }
    page.into_bytes()
}

fn main() {
    let port = int_env("HYPER_PORT", 8080, 1, 65535);
    let host = std::env::var("HYPER_HOST").unwrap_or_else(|_| "0.0.0.0".into());
    let root = find_root();
    let cfg = Arc::new(Config {
        max_upload: int_env("HYPER_MAX_UPLOAD_MB", 500, 1, 1024 * 1024) * 1024 * 1024,
        concurrency: int_env("HYPER_CONCURRENCY", 2, 1, 1024) as usize,
        queue_limit: int_env("HYPER_QUEUE", 8, 0, 100_000) as usize,
        timeout: Duration::from_millis(int_env("HYPER_TIMEOUT_MS", 600_000, 1000, 24 * 60 * 60_000)),
        token: std::env::var("HYPER_API_TOKEN").unwrap_or_default(),
        index: index_page(&root),
        root,
    });
    let slots = Arc::new(Slots { gate: Mutex::new(Gate::default()), cv: Condvar::new() });
    let server = match Server::http(format!("{host}:{port}")) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("cannot listen on {host}:{port}: {e}");
            std::process::exit(1);
        }
    };
    println!(
        "hyper-compress server on http://{host}:{port} (concurrency {}, max upload {}MB)",
        cfg.concurrency,
        cfg.max_upload / 1024 / 1024
    );
    for req in server.incoming_requests() {
        let cfg = Arc::clone(&cfg);
        let slots = Arc::clone(&slots);
        thread::spawn(move || {
            let url_path = req.url().split('?').next().unwrap_or("/").to_string();
            let method = req.method().as_str().to_string();
            match (method.as_str(), url_path.as_str()) {
                ("GET", "/") => {
                    let _ = req.respond(Response::from_data(cfg.index.clone()).with_header(header("content-type", "text/html; charset=utf-8")));
                }
                ("GET", "/healthz") => {
                    let (active, queued) = slots.counts();
                    send_json(req, 200, json!({ "ok": true, "active": active, "queued": queued }));
                }
                ("POST", "/api/verify-password") => {
                    if authorized(&req, &cfg) {
                        handle_verify(req, &cfg, &slots)
                    } else {
                        send_json(req, 401, json!({ "error": "unauthorized" }))
                    }
                }
                ("POST", "/api/compress") => {
                    if authorized(&req, &cfg) {
                        handle_compress(req, &cfg, &slots)
                    } else {
                        send_json(req, 401, json!({ "error": "unauthorized" }))
                    }
                }
                ("GET", p) => {
                    if let Some(req) = static_file(req, &cfg, p) {
                        send_json(req, 404, json!({ "error": "not_found" }));
                    }
                }
                _ => send_json(req, 404, json!({ "error": "not_found" })),
            }
        });
    }
}
