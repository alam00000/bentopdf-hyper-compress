#[path = "../../test-support/pdf.rs"]
mod pdf;

use std::fs;
use std::path::Path;
use std::process::{Command, Output};

fn hyper(dir: &Path, args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_hyper")).current_dir(dir).args(args).output().expect("run hyper")
}

fn text(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
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
fn version_and_help() {
    let dir = tempfile::tempdir().unwrap();
    let v = hyper(dir.path(), &["--version"]);
    assert!(v.status.success());
    assert_eq!(text(&v.stdout).trim(), env!("CARGO_PKG_VERSION"));
    let h = hyper(dir.path(), &["-h"]);
    assert!(h.status.success());
    assert!(text(&h.stdout).contains("--target-size"));
}

#[test]
fn bad_usage_exits_2() {
    let dir = tempfile::tempdir().unwrap();
    for args in [&[][..], &["a.pdf", "--bogus"][..], &["a.pdf", "--preset", "huge"][..]] {
        let o = hyper(dir.path(), args);
        assert_eq!(o.status.code(), Some(2), "{args:?}");
        assert!(text(&o.stderr).contains("usage: hyper"));
    }
}

#[test]
fn missing_input_file_exits_1() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    let o = hyper(dir.path(), &["nope.pdf", "out.pdf"]);
    assert_eq!(o.status.code(), Some(1));
    assert!(text(&o.stderr).starts_with("error: "));
}

#[test]
fn compresses_beside_the_input_by_default() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("scan.pdf"), pdf::image_pdf()).unwrap();
    let o = hyper(dir.path(), &["scan.pdf"]);
    assert!(o.status.success(), "{}", text(&o.stderr));
    let out = dir.path().join("scan-compressed.pdf");
    let stdout = text(&o.stdout);
    assert!(stdout.starts_with(&out.file_name().unwrap().to_string_lossy().to_string()) || stdout.contains("scan-compressed.pdf"), "{stdout}");
    assert!(stdout.contains("% smaller)"), "{stdout}");
    let before = fs::metadata(dir.path().join("scan.pdf")).unwrap().len();
    let after = fs::metadata(&out).unwrap().len();
    assert!(after < before, "{after} < {before}");
    assert!(fs::read(&out).unwrap().starts_with(b"%PDF"));
}

#[test]
fn every_preset_gives_a_valid_pdf_no_larger_than_the_input() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("in.pdf"), pdf::image_pdf()).unwrap();
    let before = fs::metadata(dir.path().join("in.pdf")).unwrap().len();
    for preset in ["low", "medium", "high", "lossless"] {
        let name = format!("{preset}.pdf");
        let o = hyper(dir.path(), &["in.pdf", &name, "--preset", preset]);
        assert!(o.status.success(), "{preset}: {}", text(&o.stderr));
        let after = fs::metadata(dir.path().join(&name)).unwrap().len();
        assert!(after <= before, "{preset}: {after} > {before}");
        assert!(pdf::expanded(&dir.path().join(&name)).contains("/Type /Page"), "{preset} not a valid PDF");
    }
}

#[test]
fn signed_input_is_returned_untouched() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("s.pdf"), pdf::signed_pdf()).unwrap();
    let o = hyper(dir.path(), &["s.pdf", "o.pdf"]);
    assert!(o.status.success());
    assert!(text(&o.stdout).contains("[signed: original preserved]"));
    assert_eq!(fs::read(dir.path().join("s.pdf")).unwrap(), fs::read(dir.path().join("o.pdf")).unwrap());
}

#[test]
fn unreachable_target_is_best_effort_and_exits_0() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("in.pdf"), pdf::image_pdf()).unwrap();
    let o = hyper(dir.path(), &["in.pdf", "o.pdf", "--target-size", "100"]);
    assert!(o.status.success());
    assert!(text(&o.stdout).contains("[target NOT met - best effort]"), "{}", text(&o.stdout));
    assert!(text(&o.stderr).contains("warning: target of 100 bytes not reached"));
}

#[test]
fn reachable_target_is_met() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("in.pdf"), pdf::image_pdf()).unwrap();
    let o = hyper(dir.path(), &["in.pdf", "o.pdf", "--target-size", "60KB"]);
    assert!(o.status.success());
    assert!(text(&o.stdout).contains("[target met]"), "{}", text(&o.stdout));
    assert!(fs::metadata(dir.path().join("o.pdf")).unwrap().len() <= 60 * 1024);
}

#[test]
fn password_flag_opens_encrypted_files() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("plain.pdf"), pdf::image_pdf()).unwrap();
    pdf::encrypt(&dir.path().join("plain.pdf"), &dir.path().join("lock.pdf"), "hunter2");
    let bad = hyper(dir.path(), &["lock.pdf", "o.pdf", "--password", "wrong"]);
    assert_eq!(bad.status.code(), Some(1));
    assert!(text(&bad.stderr).contains("decrypt_failed"), "{}", text(&bad.stderr));
    let good = hyper(dir.path(), &["lock.pdf", "o.pdf", "--password", "hunter2"]);
    assert!(good.status.success(), "{}", text(&good.stderr));
}

#[test]
fn report_is_written_as_json() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("in.pdf"), pdf::image_pdf()).unwrap();
    let o = hyper(dir.path(), &["in.pdf", "o.pdf", "--report", "r.json"]);
    assert!(o.status.success());
    let r: serde_json::Value = serde_json::from_slice(&fs::read(dir.path().join("r.json")).unwrap()).unwrap();
    assert_eq!(r["images"][0]["name"], "Im0");
    assert_eq!(r["images"][0]["before"]["width"], 500);
    assert_eq!(r["fonts"][0]["name"], "Helvetica");
    assert!(r["compressedSize"].as_u64().unwrap() < r["originalSize"].as_u64().unwrap());
}

#[test]
fn rasterize_refusal_is_a_warning() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("t.pdf"), pdf::text_pdf()).unwrap();
    let o = hyper(dir.path(), &["t.pdf", "o.pdf", "--set", "rasterizePages=true"]);
    assert!(o.status.success());
    assert!(text(&o.stderr).contains("warning: rasterize skipped"), "{}", text(&o.stderr));
}

#[test]
fn pdfa_claim_is_kept_with_preserve_and_removed_without() {
    need_engine!();
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("a.pdf"), pdf::pdfa_pdf()).unwrap();
    assert!(hyper(dir.path(), &["a.pdf", "keep.pdf", "--set", "preserveConformance=true"]).status.success());
    assert!(pdf::expanded(&dir.path().join("keep.pdf")).contains("pdfaid:part"));
    assert!(hyper(dir.path(), &["a.pdf", "drop.pdf"]).status.success());
    let dropped = pdf::expanded(&dir.path().join("drop.pdf"));
    assert!(!dropped.contains("pdfaid:part"));
    assert!(dropped.contains("x:xmpmeta"));
}
