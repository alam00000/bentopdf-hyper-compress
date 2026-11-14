#![recursion_limit = "256"]
pub mod engine;
pub mod error;
pub mod options;
pub mod pdfa;
pub mod presets;
pub mod report;
pub mod run;
pub mod target;

pub use engine::{
    Binaries, CompressInput, CompressResult, ReportRequest, compress, resolve_options,
    verify_password,
};
pub use error::{ErrorCode, HyperError, Result};
pub use options::Options;
pub use pdfa::PdfaLevel;
pub use presets::Preset;
pub use run::CancelToken;
