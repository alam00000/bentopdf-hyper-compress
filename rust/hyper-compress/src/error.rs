use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorCode {
    DecryptFailed,
    EngineError,
    Timeout,
    Cancelled,
}

impl ErrorCode {
    pub fn as_str(self) -> &'static str {
        match self {
            ErrorCode::DecryptFailed => "decrypt_failed",
            ErrorCode::EngineError => "engine_error",
            ErrorCode::Timeout => "timeout",
            ErrorCode::Cancelled => "cancelled",
        }
    }
}

#[derive(Debug, Clone)]
pub struct HyperError {
    pub code: ErrorCode,
    pub message: String,
}

impl HyperError {
    pub fn new(code: ErrorCode, message: impl Into<String>) -> Self {
        Self { code, message: message.into() }
    }

    pub fn code(code: ErrorCode) -> Self {
        Self { code, message: code.as_str().to_string() }
    }
}

impl fmt::Display for HyperError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for HyperError {}

impl From<std::io::Error> for HyperError {
    fn from(e: std::io::Error) -> Self {
        HyperError::new(ErrorCode::EngineError, e.to_string())
    }
}

pub type Result<T> = std::result::Result<T, HyperError>;
