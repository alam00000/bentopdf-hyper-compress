use std::io::{Read, Write};
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use crate::error::{ErrorCode, HyperError};

pub const DEFAULT_STAGE_TIMEOUT: Duration = Duration::from_millis(600_000);

#[derive(Clone, Default)]
pub struct CancelToken(Arc<AtomicBool>);

impl CancelToken {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.0.store(true, Ordering::SeqCst);
    }

    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::SeqCst)
    }
}

#[derive(Clone)]
pub struct Limits {
    pub stage: Duration,
    pub deadline: Option<Instant>,
    pub cancel: CancelToken,
}

impl Limits {
    pub fn aborted(&self) -> Option<HyperError> {
        if self.deadline.is_some_and(|d| Instant::now() >= d) {
            return Some(HyperError::code(ErrorCode::Timeout));
        }
        if self.cancel.is_cancelled() {
            return Some(HyperError::code(ErrorCode::Cancelled));
        }
        None
    }

    pub fn check(&self) -> Result<(), HyperError> {
        match self.aborted() {
            Some(e) => Err(e),
            None => Ok(()),
        }
    }
}

pub struct Output {
    pub ok: bool,
    pub timed_out: bool,
    pub code: Option<i32>,
    pub stdout: Vec<u8>,
    pub stderr: String,
}

fn drain<R: Read + Send + 'static>(r: Option<R>) -> thread::JoinHandle<Vec<u8>> {
    thread::spawn(move || {
        let mut buf = Vec::new();
        if let Some(mut r) = r {
            let _ = r.read_to_end(&mut buf);
        }
        buf
    })
}

pub fn run(bin: &Path, args: &[String], stdin: Option<&[u8]>, capture_stdout: bool, limits: &Limits) -> Output {
    let failed = Output { ok: false, timed_out: false, code: None, stdout: Vec::new(), stderr: String::new() };
    let mut cmd = Command::new(bin);
    cmd.args(args)
        .stdin(if stdin.is_some() { Stdio::piped() } else { Stdio::null() })
        .stdout(if capture_stdout { Stdio::piped() } else { Stdio::null() })
        .stderr(Stdio::piped());
    let Ok(mut child) = cmd.spawn() else { return failed };
    if let Some(data) = stdin {
        if let Some(mut s) = child.stdin.take() {
            let _ = s.write_all(data);
        }
    }
    let out = drain(child.stdout.take());
    let err = drain(child.stderr.take());
    let started = Instant::now();
    let mut timed_out = false;
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break Some(s),
            Ok(None) => {}
            Err(_) => break None,
        }
        if started.elapsed() >= limits.stage || limits.aborted().is_some() {
            timed_out = started.elapsed() >= limits.stage;
            let _ = child.kill();
            let _ = child.wait();
            break None;
        }
        thread::sleep(Duration::from_millis(5));
    };
    let stdout = out.join().unwrap_or_default();
    let stderr = String::from_utf8_lossy(&err.join().unwrap_or_default()).into_owned();
    match status {
        Some(s) => Output { ok: s.success(), timed_out: false, code: s.code(), stdout, stderr },
        None => Output { ok: false, timed_out, code: None, stdout, stderr },
    }
}
