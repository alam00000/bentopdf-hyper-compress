use serde::Serialize;

use crate::options::Options;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct PdfaLevel {
    pub part: u32,
    pub conformance: String,
}

impl PdfaLevel {
    pub fn label(&self) -> String {
        format!("PDF/A-{}{}", self.part, self.conformance)
    }
}

const MAX_XMP_BYTES: usize = 1024 * 1024;

fn find(hay: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if from >= hay.len() {
        return None;
    }
    hay[from..].windows(needle.len()).position(|w| w == needle).map(|p| p + from)
}

fn value_after(xmp: &str, key: &str) -> Option<char> {
    let mut rest = xmp;
    while let Some(i) = rest.find(key) {
        let after = rest[i + key.len()..].trim_start();
        let after = if let Some(a) = after.strip_prefix('=') {
            a.trim_start().strip_prefix(['"', '\'']).map(str::trim_start)
        } else {
            after.strip_prefix('>').map(str::trim_start)
        };
        if let Some(c) = after.and_then(|a| a.chars().next()) {
            return Some(c);
        }
        rest = &rest[i + key.len()..];
    }
    None
}

pub fn parse_xmp(xmp: &str) -> Option<PdfaLevel> {
    let part = value_after(xmp, "pdfaid:part").filter(char::is_ascii_digit)?;
    let conformance = value_after(xmp, "pdfaid:conformance")
        .filter(char::is_ascii_alphabetic)
        .map(|c| c.to_ascii_lowercase().to_string())
        .unwrap_or_default();
    Some(PdfaLevel { part: part.to_digit(10)?, conformance })
}

pub fn detect(bytes: &[u8]) -> Option<PdfaLevel> {
    let start = find(bytes, b"<?xpacket", 0)?;
    let end = find(bytes, b"<?xpacket end", start).unwrap_or(bytes.len());
    let stop = end.min(start + MAX_XMP_BYTES);
    let text: String = bytes[start..stop].iter().map(|&b| b as char).collect();
    parse_xmp(&text)
}

pub fn strip_claim(xmp: &str) -> String {
    let mut out = xmp.to_string();
    for key in ["part", "conformance", "rev"] {
        let open = format!("<pdfaid:{key}>");
        let close = format!("</pdfaid:{key}>");
        while let Some(i) = out.find(&open) {
            let Some(j) = out[i..].find(&close) else { break };
            let start = out[..i].trim_end().len();
            out.replace_range(start..i + j + close.len(), "");
        }
        let attr = format!("pdfaid:{key}");
        let mut from = 0;
        while let Some(rel) = out[from..].find(&attr) {
            let i = from + rel;
            let after = &out[i + attr.len()..];
            let trimmed = after.trim_start();
            if let Some(eq) = trimmed.strip_prefix('=') {
                let eq = eq.trim_start();
                if let Some(q) = eq.chars().next().filter(|c| *c == '"' || *c == '\'') {
                    if let Some(end) = eq[1..].find(q) {
                        let consumed = after.len() - eq.len() + 1 + end + 1;
                        let start = out[..i].trim_end().len();
                        out.replace_range(start..i + attr.len() + consumed, "");
                        from = start;
                        continue;
                    }
                }
            }
            from = i + attr.len();
        }
    }
    let ns = "xmlns:pdfaid";
    let mut from = 0;
    while let Some(rel) = out[from..].find(ns) {
        let i = from + rel;
        let after = &out[i + ns.len()..];
        let trimmed = after.trim_start();
        if let Some(eq) = trimmed.strip_prefix('=') {
            let eq = eq.trim_start();
            if let Some(q) = eq.chars().next().filter(|c| *c == '"' || *c == '\'') {
                if let Some(end) = eq[1..].find(q) {
                    let consumed = after.len() - eq.len() + 1 + end + 1;
                    let start = out[..i].trim_end().len();
                    out.replace_range(start..i + ns.len() + consumed, "");
                    from = start;
                    continue;
                }
            }
        }
        from = i + ns.len();
    }
    out
}

pub fn safe_options(requested: &Options, level: &PdfaLevel) -> Options {
    let mut s = requested.clone();
    s.remove_standard_fonts = false;
    s.unembed_aliased_fonts = false;
    s.remove_output_intents = false;
    s.flatten_icc = false;
    s.rasterize_pages = false;
    s.remove_metadata = false;
    s.color_target = "original".into();
    s.linearize = false;
    if level.part < 4 && s.min_version == "2.0" {
        s.min_version = "none".into();
    }
    if level.part == 1 {
        s.prefer_jpx = false;
        s.subset_fonts = false;
    }
    if level.conformance == "a" {
        s.remove_struct_tree = false;
    }
    s
}

pub fn safety_warnings(requested: &Options, safe: &Options) -> Vec<String> {
    let mut w = Vec::new();
    if requested.rasterize_pages && !safe.rasterize_pages {
        w.push("rasterizePages disabled to preserve PDF/A conformance".to_string());
    }
    if requested.brotli {
        w.push("brotli disabled to preserve PDF/A conformance".to_string());
    }
    if requested.remove_metadata && !safe.remove_metadata {
        w.push("removeMetadata refused: PDF/A requires the XMP metadata packet".to_string());
    }
    if requested.color_target != safe.color_target {
        w.push("colorTarget ignored to preserve PDF/A conformance".to_string());
    }
    if requested.linearize && !safe.linearize {
        w.push("linearize skipped to preserve PDF/A conformance".to_string());
    }
    if requested.min_version != safe.min_version {
        w.push(format!("minVersion {} refused: this PDF/A level requires a 1.x header", requested.min_version));
    }
    w
}

fn header_version(b: &[u8]) -> Option<String> {
    if b.len() < 8 || &b[..5] != b"%PDF-" {
        return None;
    }
    let v = &b[5..8];
    (v[0].is_ascii_digit() && v[1] == b'.' && v[2].is_ascii_digit()).then(|| String::from_utf8_lossy(v).into_owned())
}

pub fn raise_header(output: &mut [u8], min_version: &str) {
    let ok = min_version.len() == 3 && min_version.as_bytes()[1] == b'.';
    if !ok {
        return;
    }
    if let Some(cur) = header_version(output) {
        if cur.as_str() < min_version {
            output[5..8].copy_from_slice(min_version.as_bytes());
        }
    }
}

pub fn restore_header(source: &[u8], output: &mut [u8]) {
    if let (Some(from), Some(to)) = (header_version(source), header_version(output)) {
        if from != to {
            output[..8].copy_from_slice(&source[..8]);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_and_strips() {
        let xmp = r#"<rdf:Description xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/" pdfaid:part="2" pdfaid:conformance="B"/><x><pdfaid:part>3</pdfaid:part></x>"#;
        assert_eq!(parse_xmp(xmp), Some(PdfaLevel { part: 2, conformance: "b".into() }));
        let s = strip_claim(xmp);
        assert!(!s.contains("pdfaid"), "{s}");
        assert!(s.contains("rdf:Description"));
    }

    #[test]
    fn header_edits() {
        let mut b = b"%PDF-1.4\n".to_vec();
        raise_header(&mut b, "1.7");
        assert_eq!(&b[..8], b"%PDF-1.7");
        restore_header(b"%PDF-1.5\n", &mut b);
        assert_eq!(&b[..8], b"%PDF-1.5");
    }
}
