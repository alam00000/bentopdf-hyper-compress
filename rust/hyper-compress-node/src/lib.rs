use std::path::PathBuf;
use std::time::Duration;

use hyper_compress::{
    compress, resolve_options, verify_password, Binaries, CancelToken, CompressInput, HyperError, Preset, ReportRequest,
};
use napi::bindgen_prelude::*;
use napi_derive::napi;
use serde_json::{json, Map, Value};

#[napi]
#[derive(Default)]
pub struct CancelHandle(CancelToken);

#[napi]
impl CancelHandle {
    #[napi(constructor)]
    pub fn new() -> Self {
        CancelHandle(CancelToken::new())
    }

    #[napi]
    pub fn cancel(&self) {
        self.0.cancel();
    }
}

fn path(v: Option<&Value>) -> Option<PathBuf> {
    v.and_then(Value::as_str).filter(|s| !s.is_empty()).map(PathBuf::from)
}

fn millis(v: Option<&Value>) -> Option<Duration> {
    v.and_then(Value::as_f64).filter(|n| n.is_finite() && *n > 0.0).map(|n| Duration::from_millis(n as u64))
}

fn binaries(v: &Value) -> Binaries {
    Binaries { worker: path(v.get("worker")), qpdf: path(v.get("qpdf")), search_dirs: vec![] }
}

fn failure(e: HyperError) -> Value {
    json!({ "error": { "code": e.code.as_str(), "message": e.message } })
}

fn to_input(v: &Value, cancel: CancelToken) -> CompressInput {
    CompressInput {
        source_path: path(v.get("sourcePath")).unwrap_or_default(),
        save_path: path(v.get("savePath")),
        password: v.get("password").and_then(Value::as_str).map(str::to_string),
        preset: v.get("preset").and_then(Value::as_str).and_then(Preset::parse),
        options: v.get("options").and_then(Value::as_object).cloned().unwrap_or_default(),
        target_size_bytes: v.get("targetSizeBytes").and_then(Value::as_f64).filter(|n| n.is_finite() && *n > 0.0).map(|n| n.floor() as u64),
        report: match v.get("report") {
            Some(Value::Bool(true)) => ReportRequest::Return,
            Some(Value::String(s)) if !s.is_empty() => ReportRequest::File(PathBuf::from(s)),
            _ => ReportRequest::None,
        },
        stage_timeout: millis(v.get("timeoutMs")),
        total_timeout: millis(v.get("totalTimeoutMs")),
        cancel,
        binaries: binaries(v),
    }
}

fn spawn_json<'env, F>(env: &'env Env, work: F) -> Result<Object<'env>>
where
    F: FnOnce() -> Value + Send + 'static,
{
    let (deferred, promise) = env.create_deferred()?;
    std::thread::spawn(move || {
        let out = work().to_string();
        deferred.resolve(move |_env| Ok(out));
    });
    Ok(promise)
}

#[napi(ts_return_type = "Promise<string>")]
pub fn compress_native<'env>(env: &'env Env, input: Value, cancel: Option<&CancelHandle>) -> Result<Object<'env>> {
    let cancel = cancel.map(|c| c.0.clone()).unwrap_or_default();
    spawn_json(env, move || match compress(&to_input(&input, cancel)) {
        Ok(r) => json!({ "result": r }),
        Err(e) => failure(e),
    })
}

#[napi(ts_return_type = "Promise<string>")]
pub fn verify_password_native<'env>(env: &'env Env, input: Value) -> Result<Object<'env>> {
    spawn_json(env, move || {
        let source = path(input.get("sourcePath")).unwrap_or_default();
        let password = input.get("password").and_then(Value::as_str).unwrap_or("");
        match verify_password(&source, password, millis(input.get("timeoutMs")), &binaries(&input)) {
            Ok(valid) => json!({ "result": valid }),
            Err(e) => failure(e),
        }
    })
}

#[napi]
pub fn resolve_options_native(preset: Option<String>, options: Option<Value>) -> Value {
    let overrides: Map<String, Value> = options.and_then(|o| o.as_object().cloned()).unwrap_or_default();
    Value::Object(resolve_options(preset.as_deref().and_then(Preset::parse), &overrides).to_map())
}

#[napi]
pub fn build_tokens_native(options: Value) -> Vec<String> {
    let m = options.as_object().cloned().unwrap_or_default();
    hyper_compress::Options::normalize(&m).tokens()
}

#[napi]
pub fn engine_version() -> String {
    env!("CARGO_PKG_VERSION").to_string()
}
