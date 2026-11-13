use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use base64::Engine as _;
use serde::Serialize;
use serde_json::{json, Map, Value};

use crate::error::{ErrorCode, HyperError, Result};
use crate::options::Options;
use crate::pdfa::{self, PdfaLevel};
use crate::presets::Preset;
use crate::report::{self, Report};
use crate::run::{self, CancelToken, Limits, DEFAULT_STAGE_TIMEOUT};
use crate::target;

const EXE: &str = if cfg!(windows) { ".exe" } else { "" };

#[derive(Debug, Clone, Default)]
pub struct Binaries {
    pub worker: Option<PathBuf>,
    pub qpdf: Option<PathBuf>,
    pub search_dirs: Vec<PathBuf>,
}

impl Binaries {
    pub fn beside_exe() -> Binaries {
        let mut search_dirs = Vec::new();
        if let Some(dir) = std::env::current_exe().ok().and_then(|e| e.parent().map(Path::to_path_buf)) {
            search_dirs.push(dir.clone());
            search_dirs.extend(dir.ancestors().take(8).map(|d| d.join("cli").join("prebuilt")));
        }
        Binaries { worker: None, qpdf: None, search_dirs }
    }

    fn find(&self, explicit: &Option<PathBuf>, env: &str, name: &str) -> Option<PathBuf> {
        if let Some(p) = explicit {
            return Some(p.clone());
        }
        if let Some(p) = std::env::var_os(env).filter(|v| !v.is_empty()) {
            return Some(PathBuf::from(p));
        }
        self.search_dirs.iter().map(|d| d.join(format!("{name}{EXE}"))).find(|p| p.is_file())
    }

    fn require(&self, explicit: &Option<PathBuf>, env: &str, name: &str) -> Result<PathBuf> {
        self.find(explicit, env, name).ok_or_else(|| {
            HyperError::new(
                ErrorCode::EngineError,
                format!(
                    "no {name} binary for {}: use a platform with a native package (darwin-arm64, linux-x64, linux-arm64, win32-x64), set {env} to a binary you built (see BUILDING.md), or switch to the hyper-compress-wasm package",
                    platform()
                ),
            )
        })
    }

    fn worker(&self) -> Result<PathBuf> {
        let p = self.require(&self.worker, "HYPER_DRV", "hpdf-worker")?;
        if !p.is_file() {
            return Err(HyperError::new(ErrorCode::EngineError, format!("hpdf-worker not found at {}", p.display())));
        }
        Ok(p)
    }

    fn check_qpdf(&self) -> Result<()> {
        match self.qpdf() {
            Some(p) if !p.is_file() => Err(HyperError::new(ErrorCode::EngineError, format!("qpdf not found at {}", p.display()))),
            _ => Ok(()),
        }
    }

    fn qpdf(&self) -> Option<PathBuf> {
        self.find(&self.qpdf, "HYPER_QPDF", "qpdf")
    }

    fn require_qpdf(&self) -> Result<PathBuf> {
        self.require(&self.qpdf, "HYPER_QPDF", "qpdf")
    }
}

#[derive(Debug, Clone, Default)]
pub enum ReportRequest {
    #[default]
    None,
    Return,
    File(PathBuf),
}

#[derive(Clone, Default)]
pub struct CompressInput {
    pub source_path: PathBuf,
    pub save_path: Option<PathBuf>,
    pub password: Option<String>,
    pub preset: Option<Preset>,
    pub options: Map<String, Value>,
    pub target_size_bytes: Option<u64>,
    pub report: ReportRequest,
    pub stage_timeout: Option<Duration>,
    pub total_timeout: Option<Duration>,
    pub cancel: CancelToken,
    pub binaries: Binaries,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompressResult {
    pub output_path: PathBuf,
    pub original_size: u64,
    pub compressed_size: u64,
    pub signed: bool,
    pub signatures_removed: bool,
    pub pdfa: Option<PdfaLevel>,
    pub pdfa_preserved: bool,
    pub met_target: Option<bool>,
    pub warnings: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub report: Option<Report>,
}

pub fn resolve_options(preset: Option<Preset>, overrides: &Map<String, Value>) -> Options {
    preset.unwrap_or(Preset::Medium).options().with(overrides)
}

fn arg(p: &Path) -> String {
    std::path::absolute(p).unwrap_or_else(|_| p.to_path_buf()).to_string_lossy().into_owned()
}

fn size(p: &Path) -> u64 {
    fs::metadata(p).map(|m| m.len()).unwrap_or(0)
}

struct Driver {
    ok: bool,
    signed: bool,
    signatures_removed: bool,
    note: Option<String>,
    stderr: String,
}

fn run_driver(bins: &Binaries, input: &Path, output: &Path, tokens: &[String], limits: &Limits) -> Result<Driver> {
    let mut args = vec![arg(input), arg(output)];
    args.extend(tokens.iter().cloned());
    let o = run::run(&bins.worker()?, &args, None, false, limits);
    stage_check(&o, limits, "the engine pass")?;
    let note = o
        .stderr
        .lines()
        .find_map(|l| l.split("[hyper] ").nth(1).filter(|s| s.starts_with("rasterize skipped:")).map(str::to_string));
    Ok(Driver {
        ok: o.ok,
        signed: o.stderr.contains("SIGNED_SKIP"),
        signatures_removed: o.stderr.contains("SIGNATURES_REMOVED"),
        note,
        stderr: o.stderr,
    })
}

fn stage_check(o: &run::Output, limits: &Limits, stage: &str) -> Result<()> {
    limits.check()?;
    if o.timed_out {
        return Err(HyperError::new(
            ErrorCode::Timeout,
            format!("{stage} ran longer than the {} ms stage limit", limits.stage.as_millis()),
        ));
    }
    Ok(())
}

fn run_qpdf(bins: &Binaries, args: Vec<String>, limits: &Limits) -> Result<bool> {
    match bins.qpdf() {
        Some(bin) => {
            let o = run::run(&bin, &args, None, false, limits);
            stage_check(&o, limits, "qpdf")?;
            Ok(o.ok)
        }
        None => Ok(false),
    }
}

fn qpdf_json(bins: &Binaries, args: Vec<String>, limits: &Limits) -> Result<Option<Value>> {
    let Some(bin) = bins.qpdf() else { return Ok(None) };
    let o = run::run(&bin, &args, None, true, limits);
    stage_check(&o, limits, "qpdf")?;
    Ok(serde_json::from_slice(&o.stdout).ok())
}

fn pack(bins: &Binaries, input: &Path, output: &Path, limits: &Limits, recompress: bool, linearize: bool) -> Result<bool> {
    let mut args = vec!["--warning-exit-0".to_string(), "--object-streams=generate".to_string()];
    if recompress {
        args.push("--recompress-flate".into());
    }
    if linearize {
        args.push("--linearize".into());
    }
    args.extend(["--compression-level=9".to_string(), arg(input), arg(output)]);
    run_qpdf(bins, args, limits)
}

fn decrypt(bins: &Binaries, input: &Path, output: &Path, password: &str, limits: &Limits) -> Result<()> {
    let bin = bins.require_qpdf()?;
    let first = password.split('\n').next().unwrap_or("");
    let args = vec![
        "--warning-exit-0".to_string(),
        "--password-file=-".to_string(),
        "--decrypt".to_string(),
        arg(input),
        arg(output),
    ];
    let o = run::run(&bin, &args, Some(first.as_bytes()), false, limits);
    if o.ok {
        return Ok(());
    }
    stage_check(&o, limits, "decryption")?;
    Err(HyperError::code(ErrorCode::DecryptFailed))
}

pub fn verify_password(source: &Path, password: &str, timeout: Option<Duration>, bins: &Binaries) -> Result<bool> {
    if !source.is_file() {
        return Err(HyperError::new(ErrorCode::EngineError, format!("cannot read {}", source.display())));
    }
    let dir = tempfile::Builder::new().prefix("hyper-verify-").tempdir()?;
    let limits = Limits { stage: timeout.unwrap_or(Duration::from_millis(60_000)), deadline: None, cancel: CancelToken::new() };
    match decrypt(bins, source, &dir.path().join("dec.pdf"), password, &limits) {
        Ok(()) => Ok(true),
        Err(e) if e.code == ErrorCode::DecryptFailed => Ok(false),
        Err(e) => Err(e),
    }
}

fn requires_password(bins: &Binaries, input: &Path, limits: &Limits) -> Result<bool> {
    match bins.qpdf() {
        Some(bin) => {
            let o = run::run(&bin, &["--requires-password".to_string(), arg(input)], None, false, limits);
            stage_check(&o, limits, "qpdf")?;
            Ok(o.code == Some(0))
        }
        None => Ok(false),
    }
}

fn report_json(bins: &Binaries, p: &Path, limits: &Limits) -> Result<Option<Value>> {
    qpdf_json(
        bins,
        vec!["--json=2".into(), "--json-key=pages".into(), "--json-key=qpdf".into(), "--json-stream-data=none".into(), arg(p)],
        limits,
    )
}

enum Withdraw {
    Written,
    Unchanged,
    Failed,
}

fn withdraw_claim(bins: &Binaries, input: &Path, output: &Path, dir: &Path, limits: &Limits, linearize: bool) -> Result<Withdraw> {
    let object = |r: &str, data: bool| -> Result<Option<Value>> {
        let id = if r == "trailer" { r.to_string() } else { r.trim_end_matches(" R").replace(' ', ",") };
        let mut args = vec!["--json=2".to_string(), "--json-key=qpdf".to_string(), format!("--json-object={id}")];
        if data {
            args.extend(["--json-stream-data=inline".to_string(), "--decode-level=generalized".to_string()]);
        } else {
            args.push("--json-stream-data=none".into());
        }
        args.push(arg(input));
        let key = if r == "trailer" { r.to_string() } else { format!("obj:{r}") };
        Ok(qpdf_json(bins, args, limits)?.and_then(|j| j.get("qpdf")?.get(1)?.get(key).cloned()))
    };
    let ref_of = |o: &Option<Value>, k: &str| -> Option<String> {
        let v = o.as_ref()?.get("value")?.get(k)?.as_str()?;
        let parts: Vec<&str> = v.split(' ').collect();
        (parts.len() == 3 && parts[2] == "R" && parts[0].parse::<u64>().is_ok()).then(|| v.to_string())
    };
    let Some(root) = ref_of(&object("trailer", false)?, "/Root") else { return Ok(Withdraw::Failed) };
    let Some(meta_ref) = ref_of(&object(&root, false)?, "/Metadata") else { return Ok(Withdraw::Unchanged) };
    let Some(meta) = object(&meta_ref, true)? else { return Ok(Withdraw::Failed) };
    let (Some(dict), Some(data)) = (
        meta.get("stream").and_then(|s| s.get("dict")).and_then(Value::as_object),
        meta.get("stream").and_then(|s| s.get("data")).and_then(Value::as_str),
    ) else {
        return Ok(Withdraw::Failed);
    };
    let Ok(raw) = base64::engine::general_purpose::STANDARD.decode(data) else { return Ok(Withdraw::Failed) };
    let xmp: String = raw.iter().map(|&b| b as char).collect();
    let stripped = pdfa::strip_claim(&xmp);
    if stripped == xmp {
        return Ok(Withdraw::Unchanged);
    }
    let mut dict = dict.clone();
    for k in ["/Filter", "/DecodeParms", "/Length"] {
        dict.remove(k);
    }
    let bytes: Vec<u8> = stripped.chars().map(|c| c as u32 as u8).collect();
    let update = dir.join("withdraw.json");
    let body = json!({ "qpdf": [{ "jsonversion": 2 }, {
        format!("obj:{meta_ref}"): { "stream": { "dict": dict, "data": base64::engine::general_purpose::STANDARD.encode(bytes) } }
    }] });
    if fs::write(&update, body.to_string()).is_err() {
        return Ok(Withdraw::Failed);
    }
    let mut args = vec!["--warning-exit-0".to_string(), format!("--update-from-json={}", arg(&update)), "--object-streams=generate".to_string()];
    if linearize {
        args.push("--linearize".into());
    }
    args.extend(["--compression-level=9".to_string(), arg(input), arg(output)]);
    Ok(if run_qpdf(bins, args, limits)? && size(output) > 0 { Withdraw::Written } else { Withdraw::Failed })
}

fn same_file(a: &Path, b: &Path) -> bool {
    matches!((fs::canonicalize(a), fs::canonicalize(b)), (Ok(x), Ok(y)) if x == y)
}

fn deliver(from: &Path, to: &Path) -> Result<()> {
    if same_file(from, to) {
        return Ok(());
    }
    let staging = to.with_file_name(format!(
        ".{}.hyper-tmp",
        to.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
    ));
    fs::copy(from, &staging)?;
    fs::rename(&staging, to).inspect_err(|_| {
        let _ = fs::remove_file(&staging);
    })?;
    Ok(())
}

fn platform() -> String {
    let os = match std::env::consts::OS {
        "macos" => "darwin",
        "windows" => "win32",
        o => o,
    };
    let arch = match std::env::consts::ARCH {
        "aarch64" => "arm64",
        "x86_64" => "x64",
        a => a,
    };
    format!("{os}-{arch}")
}

fn looks_like_pdf(bytes: &[u8]) -> bool {
    bytes[..bytes.len().min(1024)].windows(5).any(|w| w == b"%PDF-")
}

fn raise_file_header(p: &Path, min_version: &str) -> Result<()> {
    if min_version == "none" {
        return Ok(());
    }
    let mut bytes = fs::read(p)?;
    let before = bytes.get(..8).map(<[u8]>::to_vec);
    pdfa::raise_header(&mut bytes, min_version);
    if before.as_deref() != bytes.get(..8) {
        fs::write(p, bytes)?;
    }
    Ok(())
}

pub fn compress(input: &CompressInput) -> Result<CompressResult> {
    if input.source_path.as_os_str().is_empty() {
        return Err(HyperError::new(ErrorCode::EngineError, "sourcePath is required"));
    }
    let bins = &input.binaries;
    let requested = resolve_options(input.preset, &input.options);
    let mut options = requested.clone();
    let mut warnings: Vec<String> = Vec::new();
    let password = input.password.clone().filter(|p| !p.is_empty());
    bins.worker()?;
    bins.check_qpdf()?;
    let source_bytes = fs::read(&input.source_path)
        .map_err(|e| HyperError::new(ErrorCode::EngineError, format!("cannot read {}: {e}", input.source_path.display())))?;
    if !looks_like_pdf(&source_bytes) {
        return Err(HyperError::new(ErrorCode::EngineError, "the input is not a PDF: no %PDF- header in its first 1024 bytes"));
    }
    let original_size = source_bytes.len() as u64;
    let claimed = pdfa::detect(&source_bytes);
    drop(source_bytes);

    let limits = Limits {
        stage: input.stage_timeout.unwrap_or(DEFAULT_STAGE_TIMEOUT),
        deadline: input.total_timeout.filter(|d| !d.is_zero()).map(|d| Instant::now() + d),
        cancel: input.cancel.clone(),
    };

    let preserving = options.preserve_conformance && claimed.is_some();
    if let (true, Some(level)) = (preserving, &claimed) {
        options = pdfa::safe_options(&options, level);
        warnings.extend(pdfa::safety_warnings(&requested, &options));
    }
    options.preserve_conformance = preserving;
    let want_report = !matches!(input.report, ReportRequest::None);
    let mut tokens = options.tokens();
    if want_report {
        tokens.push("71=1".into());
    }

    let final_path = match &input.save_path {
        Some(p) if !p.as_os_str().is_empty() => p.clone(),
        _ => {
            let src = &input.source_path;
            let stem = src.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
            let ext = src.extension().map(|e| format!(".{}", e.to_string_lossy())).unwrap_or_else(|| ".pdf".into());
            src.with_file_name(format!("{stem}-compressed{ext}"))
        }
    };

    let tmp = tempfile::Builder::new().prefix("hyper-").tempdir()?;
    let dir = tmp.path();
    let drv_out = dir.join("drv.pdf");
    let pack_out = dir.join("pack.pdf");

    let mut work_in = input.source_path.clone();
    let mut fallback = input.source_path.clone();
    if let Some(pw) = &password {
        let dec = dir.join("dec.pdf");
        decrypt(bins, &input.source_path, &dec, pw, &limits)?;
        work_in = dec.clone();
        fallback = dec;
    }
    let baseline = size(&fallback);
    let skip_pack = preserving;

    let attempt = |o: &Options, drv: &Path, packed: &Path| -> Result<Option<PathBuf>> {
        let r = run_driver(bins, &work_in, drv, &o.tokens(), &limits)?;
        if !r.ok || r.signed {
            return Ok(None);
        }
        if skip_pack {
            return Ok(Some(drv.to_path_buf()));
        }
        if pack(bins, drv, packed, &limits, false, o.linearize)? && size(packed) > 0 {
            return Ok(Some(packed.to_path_buf()));
        }
        Ok(Some(drv.to_path_buf()))
    };

    let target = input.target_size_bytes.filter(|t| *t > 0);
    let drv = run_driver(bins, &work_in, &drv_out, &tokens, &limits)?;
    limits.check()?;
    if !drv.ok && password.is_none() && requires_password(bins, &input.source_path, &limits)? {
        return Err(HyperError::new(ErrorCode::DecryptFailed, "the PDF is encrypted and needs a password"));
    }
    if let Some(n) = &drv.note {
        warnings.push(n.clone());
    }

    let make_report = |out: &Path, compressed: u64, warnings: &mut Vec<String>| -> Result<Option<Report>> {
        if !want_report {
            return Ok(None);
        }
        let (Some(before), Some(after)) = (report_json(bins, &work_in, &limits)?, report_json(bins, out, &limits)?) else {
            warnings.push("report unavailable: the PDF structure could not be read".into());
            return Ok(None);
        };
        let r = report::build(&before, &after, original_size, compressed, &report::image_notes(&drv.stderr));
        if let ReportRequest::File(p) = &input.report {
            fs::write(p, serde_json::to_string_pretty(&r).unwrap_or_default())?;
        }
        Ok(Some(r))
    };

    if drv.ok && drv.signed {
        deliver(&input.source_path, &final_path)?;
        let signed_size = size(&final_path);
        if let Some(t) = target.filter(|t| signed_size > *t) {
            warnings.push(format!(
                "target of {t} bytes not reached: signed documents are returned untouched; set removeSignatures to compress them"
            ));
        }
        let report = make_report(&final_path, signed_size, &mut warnings)?;
        return Ok(CompressResult {
            output_path: final_path,
            original_size,
            compressed_size: signed_size,
            signed: true,
            signatures_removed: false,
            pdfa_preserved: claimed.is_some(),
            pdfa: claimed,
            met_target: target.map(|t| signed_size <= t),
            warnings,
            report,
        });
    }

    let mut packed: Option<PathBuf> = None;
    if drv.ok {
        packed = Some(if skip_pack {
            drv_out.clone()
        } else if pack(bins, &drv_out, &pack_out, &limits, false, options.linearize)? && size(&pack_out) > 0 {
            pack_out.clone()
        } else {
            drv_out.clone()
        });
    } else if !skip_pack && pack(bins, &work_in, &pack_out, &limits, true, options.linearize)? && size(&pack_out) > 0 {
        packed = Some(pack_out.clone());
    }

    if let (Some(t), Some(p), false) = (target, packed.clone(), preserving) {
        let mut smallest = (p.clone(), size(&p));
        let mut best: Option<(PathBuf, u64)> = None;
        if smallest.1 <= t {
            best = Some(smallest.clone());
        } else {
            let a_drv = dir.join("tgt-drv.pdf");
            let a_pack = dir.join("tgt-pack.pdf");
            let keep_fit = dir.join("tgt-fit.pdf");
            let keep_small = dir.join("tgt-small.pdf");
            let mut base = options.clone();
            base.lossless = false;
            let mut lo = target::QUALITY_FLOOR;
            let mut hi = target::start_quality(&base) - 1;
            let mut steps = 0;
            while lo <= hi && steps < target::QUALITY_STEPS {
                steps += 1;
                limits.check()?;
                let q = (lo + hi).div_euclid(2);
                let mut o = base.clone();
                o.image_quality = q;
                let Some(c) = attempt(&o, &a_drv, &a_pack)? else { break };
                let s = size(&c);
                if s > 0 && s < smallest.1 {
                    fs::copy(&c, &keep_small)?;
                    smallest = (keep_small.clone(), s);
                }
                if s > 0 && s <= t {
                    fs::copy(&c, &keep_fit)?;
                    best = Some((keep_fit.clone(), s));
                    lo = q + 1;
                } else {
                    hi = q - 1;
                }
            }
            for rung in target::ladder(&base) {
                if best.is_some() {
                    break;
                }
                limits.check()?;
                let Some(c) = attempt(&rung, &a_drv, &a_pack)? else { break };
                let s = size(&c);
                if s > 0 && s <= t {
                    fs::copy(&c, &keep_fit)?;
                    best = Some((keep_fit.clone(), s));
                } else if s > 0 && s < smallest.1 {
                    fs::copy(&c, &keep_small)?;
                    smallest = (keep_small.clone(), s);
                }
            }
        }
        packed = Some(best.map(|b| b.0).unwrap_or(smallest.0));
    }

    limits.check()?;

    let mut used_engine = false;
    let mut final_source = fallback.clone();
    if let Some(p) = &packed {
        let s = size(p);
        if s > 0 && s < baseline {
            final_source = p.clone();
            used_engine = true;
        }
    }

    let mut claim_kept = false;
    if let (Some(level), false, true) = (&claimed, preserving, used_engine) {
        let withdrawn = dir.join("withdrawn.pdf");
        let outcome = withdraw_claim(bins, &final_source, &withdrawn, dir, &limits, options.linearize)?;
        limits.check()?;
        match outcome {
            Withdraw::Written if size(&withdrawn) > 0 && size(&withdrawn) < baseline => final_source = withdrawn,
            Withdraw::Unchanged => {}
            _ => {
                final_source = fallback.clone();
                used_engine = false;
                claim_kept = true;
                warnings.push(format!(
                    "the {} claim could not be removed from the compressed file, so the original was kept; set preserveConformance to compress it as PDF/A",
                    level.label()
                ));
            }
        }
    }

    if final_source != input.source_path && size(&final_source) >= original_size {
        final_source = input.source_path.clone();
        used_engine = false;
    }

    if preserving && final_source != input.source_path {
        let src = fs::read(&fallback)?;
        let mut out = if final_source == fallback { src.clone() } else { fs::read(&final_source)? };
        pdfa::restore_header(&src, &mut out);
        let staged = dir.join("final.pdf");
        fs::write(&staged, out)?;
        deliver(&staged, &final_path)?;
    } else {
        deliver(&final_source, &final_path)?;
    }
    raise_file_header(&final_path, &options.min_version)?;
    if !used_engine && !claim_kept {
        if options.color_target != "original" && !options.lossless {
            warnings.push(format!(
                "colorTarget {} not applied: the converted file was not smaller than the original, so the original was kept",
                options.color_target
            ));
        }
        if options.linearize {
            warnings.push("linearize not applied: the optimized file was not smaller than the original, so the original was kept".into());
        }
    }

    let final_size = size(&final_path);
    let met_target = target.map(|t| final_size <= t);
    if let (Some(t), Some(false)) = (target, met_target) {
        warnings.push(if preserving {
            format!("target of {t} bytes not reached: {final_size} bytes; no size search runs while preserving PDF/A, so turn off preserveConformance to let Hyper go further")
        } else {
            target::miss_warning(final_size, t)
        });
    }
    let removed = drv.signatures_removed && used_engine;
    if removed {
        warnings.push("digital signatures removed so the document could be compressed".into());
    }
    let report = make_report(&final_path, final_size, &mut warnings)?;

    Ok(CompressResult {
        output_path: final_path,
        original_size,
        compressed_size: final_size,
        signed: drv.signatures_removed && !used_engine,
        signatures_removed: removed,
        pdfa_preserved: preserving || (claimed.is_some() && !used_engine),
        pdfa: claimed,
        met_target,
        warnings,
        report,
    })
}
