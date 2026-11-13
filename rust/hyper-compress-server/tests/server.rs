#[path = "../../test-support/pdf.rs"]
mod pdf;

use std::collections::HashMap;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

struct Server {
    child: Child,
    port: u16,
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

fn start(env: &[(&str, &str)]) -> Server {
    let port = TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port();
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_hyper-server"));
    cmd.env("HYPER_HOST", "127.0.0.1").env("HYPER_PORT", port.to_string()).stdout(Stdio::null()).stderr(Stdio::null());
    for (k, v) in env {
        cmd.env(k, v);
    }
    let server = Server { child: cmd.spawn().unwrap(), port };
    let started = Instant::now();
    while TcpStream::connect(("127.0.0.1", port)).is_err() {
        assert!(started.elapsed() < Duration::from_secs(10), "server did not start");
        thread::sleep(Duration::from_millis(20));
    }
    server
}

struct Resp {
    status: u16,
    headers: HashMap<String, String>,
    body: Vec<u8>,
}

impl Resp {
    fn json(&self) -> serde_json::Value {
        serde_json::from_slice(&self.body).unwrap_or(serde_json::Value::Null)
    }
    fn header(&self, k: &str) -> &str {
        self.headers.get(k).map(String::as_str).unwrap_or("<missing>")
    }
}

fn request(s: &Server, method: &str, path: &str, headers: &[(&str, &str)], body: &[u8]) -> Resp {
    let mut stream = TcpStream::connect(("127.0.0.1", s.port)).unwrap();
    let mut head = format!("{method} {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: {}\r\n", body.len());
    for (k, v) in headers {
        head.push_str(&format!("{k}: {v}\r\n"));
    }
    head.push_str("\r\n");
    stream.write_all(head.as_bytes()).unwrap();
    let _ = stream.write_all(body);
    let mut raw = Vec::new();
    stream.read_to_end(&mut raw).unwrap();
    let split = raw.windows(4).position(|w| w == b"\r\n\r\n").expect("no header end");
    let head = String::from_utf8_lossy(&raw[..split]).into_owned();
    let mut lines = head.lines();
    let status = lines.next().unwrap().split(' ').nth(1).unwrap().parse().unwrap();
    let headers = lines.filter_map(|l| l.split_once(':')).map(|(k, v)| (k.trim().to_ascii_lowercase(), v.trim().to_string())).collect();
    let mut body = raw[split + 4..].to_vec();
    if head.to_ascii_lowercase().contains("transfer-encoding: chunked") {
        body = dechunk(&body);
    }
    Resp { status, headers, body }
}

fn dechunk(mut b: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    while let Some(end) = b.windows(2).position(|w| w == b"\r\n") {
        let size = usize::from_str_radix(String::from_utf8_lossy(&b[..end]).trim(), 16).unwrap_or(0);
        if size == 0 {
            break;
        }
        out.extend(&b[end + 2..end + 2 + size]);
        b = &b[end + 2 + size + 2..];
    }
    out
}

fn contains(hay: &[u8], needle: &[u8]) -> bool {
    hay.windows(needle.len()).any(|w| w == needle)
}

fn post(s: &Server, path: &str, body: &[u8]) -> Resp {
    request(s, "POST", path, &[], body)
}

macro_rules! need_engine {
    () => {
        if !pdf::has_engine() {
            eprintln!("skipped: engine binaries not built in cli/prebuilt");
            return;
        }
    };
}

#[test]
fn healthz_index_static_and_not_found() {
    let s = start(&[]);
    let h = request(&s, "GET", "/healthz", &[], b"");
    assert_eq!(h.status, 200);
    assert_eq!(h.json(), serde_json::json!({ "ok": true, "active": 0, "queued": 0 }));
    let index = request(&s, "GET", "/", &[], b"");
    assert_eq!(index.status, 200);
    let page = String::from_utf8_lossy(&index.body);
    assert!(page.contains("createHttpEngine") && !page.contains("createWasmEngine"));
    let js = request(&s, "GET", "/web/app.js", &[], b"");
    assert_eq!(js.status, 200);
    assert!(js.header("content-type").starts_with("text/javascript"));
    assert_eq!(request(&s, "GET", "/web/../package.json", &[], b"").status, 404);
    assert_eq!(request(&s, "GET", "/web/missing.js", &[], b"").status, 404);
    assert_eq!(request(&s, "GET", "/api/compress", &[], b"").json()["error"], "not_found");
    assert_eq!(request(&s, "DELETE", "/healthz", &[], b"").status, 404);
}

#[test]
fn request_validation_errors() {
    let s = start(&[]);
    let pdf = pdf::text_pdf();
    let cases = [
        ("/api/compress?preset=huge", "bad_preset", 400),
        ("/api/compress?preset=custom", "options_required", 400),
        ("/api/compress?options=notjson", "bad_options", 400),
        ("/api/compress?options=%5B1%5D", "bad_options", 400),
        ("/api/compress?targetSizeBytes=-3", "bad_target_size", 400),
        ("/api/compress?targetSizeBytes=abc", "bad_target_size", 400),
    ];
    for (path, code, status) in cases {
        let r = post(&s, path, &pdf);
        assert_eq!((r.status, r.json()["error"].as_str().unwrap_or("")), (status, code), "{path}");
    }
    let long = format!("/api/compress?options=%7B%22a%22%3A%22{}%22%7D", "x".repeat(4100));
    assert_eq!(post(&s, &long, &pdf).json()["error"], "bad_options");
    let bad_preset = post(&s, "/api/compress?preset=huge", &pdf).json();
    assert_eq!(bad_preset["presets"], serde_json::json!(["low", "medium", "high", "lossless", "custom"]));
    assert_eq!(post(&s, "/api/compress", b"hello").json()["error"], "not_a_pdf");
}

#[test]
fn compresses_with_headers() {
    need_engine!();
    let s = start(&[]);
    let input = pdf::image_pdf();
    let r = post(&s, "/api/compress?preset=high", &input);
    assert_eq!(r.status, 200, "{}", String::from_utf8_lossy(&r.body));
    assert_eq!(r.header("content-type"), "application/pdf");
    assert!(r.body.starts_with(b"%PDF"));
    assert_eq!(r.header("x-original-size"), input.len().to_string());
    assert_eq!(r.header("x-compressed-size"), r.body.len().to_string());
    assert!(r.body.len() < input.len());
    assert_eq!(r.header("x-signed"), "false");
    assert_eq!(r.header("x-pdfa"), "");
    assert_eq!(r.header("x-met-target"), "");
    assert_eq!(r.header("x-warnings"), "");
}

#[test]
fn custom_options_and_shorthands() {
    need_engine!();
    let s = start(&[]);
    let input = pdf::image_pdf();
    let custom = post(&s, "/api/compress?preset=custom&options=%7B%22imageQuality%22%3A35%2C%22grayscale%22%3Atrue%7D", &input);
    assert_eq!(custom.status, 200);
    let text = pdf::rich_text_pdf();
    let brotli = post(&s, "/api/compress?preset=lossless&brotli=true", &text);
    assert_eq!(brotli.status, 200);
    assert!(contains(&brotli.body, b"/BrotliDecode"), "no Brotli streams");
    let plain = post(&s, "/api/compress?preset=lossless", &text);
    assert!(!contains(&plain.body, b"/BrotliDecode"));
}

#[test]
fn signed_pdfa_and_target_headers() {
    need_engine!();
    let s = start(&[]);
    let signed = pdf::signed_pdf();
    let r = post(&s, "/api/compress", &signed);
    assert_eq!(r.header("x-signed"), "true");
    assert_eq!(r.body, signed);
    let a = post(&s, "/api/compress?preservePdfa=true", &pdf::pdfa_pdf());
    assert_eq!(a.header("x-pdfa"), "2b");
    let t = post(&s, "/api/compress?targetSizeBytes=100", &pdf::image_pdf());
    assert_eq!(t.header("x-met-target"), "false");
    let warnings = t.header("x-warnings").replace("%20", " ").replace("%3A", ":");
    assert!(warnings.contains("target of 100 bytes not reached"), "{warnings}");
}

#[test]
fn passwords_and_verify() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("p.pdf"), pdf::image_pdf()).unwrap();
    pdf::encrypt(&dir.path().join("p.pdf"), &dir.path().join("l.pdf"), "hunter2");
    let locked = std::fs::read(dir.path().join("l.pdf")).unwrap();
    let s = start(&[]);
    let wrong = request(&s, "POST", "/api/compress", &[("X-Password", "nope")], &locked);
    assert_eq!((wrong.status, wrong.json()["error"].as_str().unwrap()), (400, "decrypt_failed"));
    let right = request(&s, "POST", "/api/compress", &[("X-Password", "hunter2")], &locked);
    assert_eq!(right.status, 200);
    assert_eq!(request(&s, "POST", "/api/verify-password", &[("X-Password", "hunter2")], &locked).json()["valid"], true);
    assert_eq!(request(&s, "POST", "/api/verify-password", &[("X-Password", "nope")], &locked).json()["valid"], false);
}

#[test]
fn api_token_guards_only_the_api() {
    let s = start(&[("HYPER_API_TOKEN", "sekret")]);
    let pdf = pdf::text_pdf();
    assert_eq!(post(&s, "/api/compress?preset=huge", &pdf).json()["error"], "unauthorized");
    assert_eq!(post(&s, "/api/verify-password", &pdf).status, 401);
    assert_eq!(request(&s, "POST", "/api/compress?preset=huge", &[("Authorization", "Bearer wrong")], &pdf).status, 401);
    assert_eq!(request(&s, "POST", "/api/compress?preset=huge", &[("Authorization", "Bearer sekret")], &pdf).status, 400);
    assert_eq!(request(&s, "POST", "/api/compress?preset=huge", &[("x-api-token", "sekret")], &pdf).status, 400);
    assert_eq!(request(&s, "GET", "/healthz", &[], b"").status, 200);
    assert_eq!(request(&s, "GET", "/", &[], b"").status, 200);
}

#[test]
fn upload_limit_returns_413() {
    let s = start(&[("HYPER_MAX_UPLOAD_MB", "1")]);
    let mut big = b"%PDF-1.7\n".to_vec();
    big.resize(1024 * 1024 + 10, b' ');
    let r = post(&s, "/api/compress", &big);
    assert_eq!((r.status, r.json()["maxBytes"].as_u64()), (413, Some(1024 * 1024)));
}

#[test]
fn full_queue_returns_429() {
    let s = start(&[("HYPER_CONCURRENCY", "1"), ("HYPER_QUEUE", "0")]);
    let mut holder = TcpStream::connect(("127.0.0.1", s.port)).unwrap();
    holder
        .write_all(b"POST /api/compress HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000000\r\n\r\n%PDF")
        .unwrap();
    let started = Instant::now();
    loop {
        let h = request(&s, "GET", "/healthz", &[], b"").json();
        if h["active"] == 1 {
            break;
        }
        assert!(started.elapsed() < Duration::from_secs(5), "slot never taken: {h}");
        thread::sleep(Duration::from_millis(20));
    }
    let r = post(&s, "/api/compress", &pdf::text_pdf());
    assert_eq!((r.status, r.json()["error"].as_str().unwrap()), (429, "busy"));
    drop(holder);
}
