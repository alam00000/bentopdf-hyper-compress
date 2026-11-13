#![allow(dead_code)]
use std::path::PathBuf;

pub fn prebuilt() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../cli/prebuilt")
}

pub fn has_engine() -> bool {
    let exe = if cfg!(windows) { ".exe" } else { "" };
    let dir = prebuilt();
    dir.join(format!("hpdf-worker{exe}")).is_file() && dir.join(format!("qpdf{exe}")).is_file()
}

fn build(objects: &[Vec<u8>], trailer_extra: &str) -> Vec<u8> {
    let mut out = b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n".to_vec();
    let mut offsets = Vec::new();
    for (i, o) in objects.iter().enumerate() {
        offsets.push(out.len());
        out.extend(format!("{} 0 obj\n", i + 1).as_bytes());
        out.extend(o);
        out.extend(b"\nendobj\n");
    }
    let xref = out.len();
    out.extend(format!("xref\n0 {}\n0000000000 65535 f \n", objects.len() + 1).as_bytes());
    for off in offsets {
        out.extend(format!("{off:010} 00000 n \n").as_bytes());
    }
    out.extend(format!("trailer\n<< /Size {} /Root 1 0 R{trailer_extra} >>\nstartxref\n{xref}\n%%EOF\n", objects.len() + 1).as_bytes());
    out
}

fn stream(dict: &str, data: &[u8]) -> Vec<u8> {
    let mut v = format!("<< {dict} /Length {} >>\nstream\n", data.len()).into_bytes();
    v.extend(data);
    v.extend(b"\nendstream");
    v
}

fn image_pixels(w: usize, h: usize) -> Vec<u8> {
    let mut px = Vec::with_capacity(w * h * 3);
    for y in 0..h {
        for x in 0..w {
            let n = ((x * 7919 + y * 104729) ^ (x * y)) as u32;
            px.push((x * 255 / w) as u8 ^ (n & 15) as u8);
            px.push((y * 255 / h) as u8 ^ ((n >> 4) & 15) as u8);
            px.push(((x + y) * 255 / (w + h)) as u8);
        }
    }
    px
}

pub fn image_pdf() -> Vec<u8> {
    let content = b"q 560 0 0 720 26 36 cm /Im0 Do Q BT /F1 24 Tf 40 700 Td (Scanned text) Tj ET";
    build(
        &[
            b"<< /Type /Catalog /Pages 2 0 R >>".to_vec(),
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> /Font << /F1 6 0 R >> >> >>".to_vec(),
            stream("", content),
            stream("/Type /XObject /Subtype /Image /Width 500 /Height 400 /ColorSpace /DeviceRGB /BitsPerComponent 8", &image_pixels(500, 400)),
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>".to_vec(),
        ],
        "",
    )
}

pub fn text_pdf() -> Vec<u8> {
    let content = b"BT /F1 24 Tf 40 700 Td (Hyper Compress) Tj ET";
    build(
        &[
            b"<< /Type /Catalog /Pages 2 0 R >>".to_vec(),
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>".to_vec(),
            stream("", content),
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>".to_vec(),
        ],
        "",
    )
}

pub fn signed_pdf() -> Vec<u8> {
    let content = b"BT /F1 24 Tf 40 700 Td (Signed) Tj ET";
    build(
        &[
            b"<< /Type /Catalog /Pages 2 0 R /Perms << /DocMDP 6 0 R >> >>".to_vec(),
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>".to_vec(),
            stream("", content),
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>".to_vec(),
            b"<< /Type /Sig /Filter /Adobe.PPKLite >>".to_vec(),
        ],
        "",
    )
}

pub fn pdfa_pdf() -> Vec<u8> {
    let xmp = concat!(
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>",
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">",
        "<rdf:Description xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" pdfaid:part=\"2\" pdfaid:conformance=\"B\"/>",
        "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>"
    );
    let content = "BT /F1 24 Tf 40 700 Td (Archive) Tj ET\n".repeat(3000);
    build(
        &[
            b"<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>".to_vec(),
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>".to_vec(),
            stream("", content.as_bytes()),
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>".to_vec(),
            stream("/Type /Metadata /Subtype /XML", xmp.as_bytes()),
        ],
        "",
    )
}

pub fn encrypt(input: &std::path::Path, output: &std::path::Path, password: &str) {
    let exe = if cfg!(windows) { ".exe" } else { "" };
    let ok = std::process::Command::new(prebuilt().join(format!("qpdf{exe}")))
        .args(["--encrypt", password, password, "256", "--"])
        .arg(input)
        .arg(output)
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    assert!(ok, "qpdf --encrypt failed");
}

pub fn expanded(bytes_path: &std::path::Path) -> String {
    let exe = if cfg!(windows) { ".exe" } else { "" };
    let out = std::process::Command::new(prebuilt().join(format!("qpdf{exe}")))
        .args(["--qdf", "--object-streams=disable"])
        .arg(bytes_path)
        .arg("-")
        .output()
        .expect("qpdf");
    String::from_utf8_lossy(&out.stdout).into_owned()
}

pub fn rich_text_pdf() -> Vec<u8> {
    let content: String = (0..400)
        .map(|i| format!("BT /F1 10 Tf 40 {} Td (line {i}: the quick brown fox jumps over the lazy dog) Tj ET\n", 720 - (i % 60) * 11))
        .collect();
    build(
        &[
            b"<< /Type /Catalog /Pages 2 0 R >>".to_vec(),
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".to_vec(),
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>".to_vec(),
            stream("", content.as_bytes()),
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>".to_vec(),
        ],
        "",
    )
}
