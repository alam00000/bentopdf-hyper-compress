use std::path::PathBuf;
use std::process::ExitCode;

use hyper_compress::{compress, Binaries, CompressInput, Preset, ReportRequest};
use serde_json::{Map, Value};

const USAGE: &str = "usage: hyper <input.pdf> [output.pdf] [--preset low|medium|high|lossless]
                                      [--password PW] [--set key=value ...]
                                      [--brotli] [--mrc off|auto|force]
                                      [--target-size N[KB|MB]] [--report FILE]
";

fn help() -> String {
    format!(
        "hyper - high fidelity, content preserving PDF compression

{USAGE}
options:
  --preset <name>       low, medium (default), high or lossless
  --password <pw>       password for an encrypted input
  --target-size <size>  aim for a size, e.g. 2MB, 500KB or a byte count
  --brotli              PDF 2.0 Brotli streams; the reader must support them
  --mrc <mode>          mixed raster content for scans: off (default), auto
                        (single-image scanned pages) or force (every page)
  --report <file.json>  write a JSON report of fonts, images and cleanups
  --set <key=value>     override any engine option; repeatable
  -h, --help            show this help
  -v, --version         show the version

Leave out the output path and the result is written next to the input as
<input>-compressed.pdf.

examples:
  hyper report.pdf
  hyper in.pdf out.pdf
  hyper in.pdf out.pdf --preset high
  hyper scan.pdf out.pdf --target-size 2MB
  hyper scan.pdf out.pdf --mrc auto --set mrcBackgroundDpi=70
  hyper locked.pdf out.pdf --password hunter2
  hyper in.pdf out.pdf --set grayscale=true --set imageQuality=40
  hyper in.pdf out.pdf --set colorTarget=cmyk --set linearize=true
  hyper in.pdf out.pdf --report report.json

The output is never larger than the input, signed documents are returned
untouched, and anything skipped for safety is reported as a warning.

exit codes: 0 success, 1 error, 2 bad usage
options reference: https://hyper.bentopdf.com/docs/options
"
    )
}

#[derive(Default)]
struct Parsed {
    input: Option<String>,
    output: Option<String>,
    preset: Option<Preset>,
    password: Option<String>,
    overrides: Map<String, Value>,
    target: Option<u64>,
    report: Option<String>,
    help: bool,
    version: bool,
}

enum Usage {
    Error(String),
}

fn parse_size(raw: &str) -> Option<u64> {
    let s = raw.trim().to_ascii_lowercase();
    let split = s.find(|c: char| !(c.is_ascii_digit() || c == '.')).unwrap_or(s.len());
    let (num, unit) = s.split_at(split);
    let unit = unit.trim();
    if num.is_empty() || num.starts_with('.') || num.ends_with('.') || num.matches('.').count() > 1 {
        return None;
    }
    let mult = match unit {
        "" | "b" => 1.0,
        "k" | "kb" => 1024.0,
        "m" | "mb" => 1024.0 * 1024.0,
        _ => return None,
    };
    Some((num.parse::<f64>().ok()? * mult).floor() as u64)
}

fn parse(argv: &[String]) -> Result<Parsed, Usage> {
    let mut p = Parsed::default();
    let mut positional = Vec::new();
    let mut it = argv.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--help" | "-h" => p.help = true,
            "--version" | "-v" | "-V" => p.version = true,
            "--preset" => {
                p.preset = Some(
                    it.next()
                        .and_then(|v| Preset::parse(v))
                        .ok_or_else(|| Usage::Error("--preset expects one of low, medium, high, lossless".into()))?,
                )
            }
            "--password" => p.password = it.next().cloned(),
            "--brotli" => {
                p.overrides.insert("brotli".into(), Value::Bool(true));
            }
            "--mrc" => {
                let v = it.next().map(String::as_str).unwrap_or("");
                if !["off", "auto", "force"].contains(&v) {
                    return Err(Usage::Error("--mrc expects off, auto or force".into()));
                }
                p.overrides.insert("mrc".into(), Value::String(v.into()));
            }
            "--target-size" => {
                let n = it
                    .next()
                    .and_then(|v| parse_size(v))
                    .ok_or_else(|| Usage::Error("--target-size expects a size like 2MB, 500KB or a byte count".into()))?;
                if n == 0 {
                    return Err(Usage::Error("--target-size must be greater than zero".into()));
                }
                p.target = Some(n);
            }
            "--report" => {
                p.report = Some(
                    it.next().filter(|v| !v.is_empty()).cloned().ok_or_else(|| Usage::Error("--report expects a file path".into()))?,
                )
            }
            "--set" => {
                let kv = it.next().map(String::as_str).unwrap_or("");
                let eq = kv.find('=').filter(|i| *i > 0).ok_or_else(|| Usage::Error("--set expects key=value".into()))?;
                let (key, raw) = (&kv[..eq], &kv[eq + 1..]);
                let value = match raw {
                    "true" => Value::Bool(true),
                    "false" => Value::Bool(false),
                    r => match r.trim().parse::<f64>().ok().filter(|n| n.is_finite() && !r.trim().is_empty()) {
                        Some(n) => serde_json::Number::from_f64(n).map(Value::Number).unwrap_or(Value::String(r.into())),
                        None => Value::String(r.into()),
                    },
                };
                p.overrides.insert(key.into(), value);
            }
            s if s.starts_with('-') => return Err(Usage::Error(format!("unknown flag {s}"))),
            _ => positional.push(a.clone()),
        }
    }
    p.input = positional.first().cloned();
    p.output = positional.get(1).cloned();
    Ok(p)
}

fn usage(message: &str) -> ExitCode {
    eprint!("error: {message}\n{USAGE}run \"hyper --help\" for the full option list\n");
    ExitCode::from(2)
}

fn main() -> ExitCode {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    let p = match parse(&argv) {
        Ok(p) => p,
        Err(Usage::Error(m)) => return usage(&m),
    };
    if p.help {
        print!("{}", help());
        return ExitCode::SUCCESS;
    }
    if p.version {
        println!("{}", env!("CARGO_PKG_VERSION"));
        return ExitCode::SUCCESS;
    }
    let Some(input) = p.input else { return usage("an input PDF is required") };
    let request = CompressInput {
        source_path: PathBuf::from(input),
        save_path: p.output.map(PathBuf::from),
        password: p.password,
        preset: Some(p.preset.unwrap_or(Preset::Medium)),
        options: p.overrides,
        target_size_bytes: p.target,
        report: p.report.map(|r| ReportRequest::File(PathBuf::from(r))).unwrap_or_default(),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    match compress(&request) {
        Ok(r) => {
            for w in &r.warnings {
                eprintln!("warning: {w}");
            }
            let pct = if r.original_size > 0 {
                ((1.0 - r.compressed_size as f64 / r.original_size as f64) * 100.0 + 0.5).floor() as i64
            } else {
                0
            };
            let signed = if r.signed { " [signed: original preserved]" } else { "" };
            let target = match r.met_target {
                None => "",
                Some(true) => " [target met]",
                Some(false) => " [target NOT met - best effort]",
            };
            println!("{}\n  {} -> {} bytes ({pct}% smaller){signed}{target}", r.output_path.display(), r.original_size, r.compressed_size);
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {}", e.message);
            ExitCode::from(1)
        }
    }
}
