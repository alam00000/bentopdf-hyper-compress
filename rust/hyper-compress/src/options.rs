use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Options {
    pub image_quality: i64,
    pub max_dpi: i64,
    pub max_dpi_color: i64,
    pub max_dpi_gray: i64,
    pub max_dpi_mono: i64,
    pub threshold_dpi: i64,
    pub color_target: String,
    pub jpeg_subsampling: String,
    pub jpeg_progressive: bool,
    pub speed: String,
    pub lossless: bool,
    pub grayscale: bool,
    pub force_downsample: bool,
    pub reduce_color: bool,
    pub clip_images: bool,
    pub remove_alternates: bool,
    pub flatten_icc: bool,
    pub prefer_jpx: bool,
    pub subset_fonts: bool,
    pub remove_standard_fonts: bool,
    pub unembed_aliased_fonts: bool,
    pub rasterize_pages: bool,
    pub rasterize_dpi: i64,
    pub rasterize_quality: i64,
    pub rasterize_text: bool,
    pub merge_fonts: bool,
    pub annotations: String,
    pub remove_annots: bool,
    pub flatten_forms: bool,
    pub flatten_links: bool,
    pub remove_signatures: bool,
    pub remove_thumbnails: bool,
    pub remove_app_data: bool,
    pub remove_struct_tree: bool,
    pub remove_threads: bool,
    pub remove_spider_info: bool,
    pub remove_output_intents: bool,
    pub remove_metadata: bool,
    pub remove_bookmarks: bool,
    pub remove_attachments: bool,
    pub remove_actions: bool,
    pub remove_named_dests: bool,
    pub remove_print_settings: bool,
    pub remove_search_index: bool,
    pub remove_signature_appearances: bool,
    pub preserve_conformance: bool,
    pub brotli: bool,
    pub linearize: bool,
    pub min_version: String,
    pub mrc: String,
    pub mrc_background_dpi: i64,
    pub mrc_background_quality: i64,
    pub mrc_foreground_quality: i64,
    pub mrc_detect_photos: bool,
    pub jbig2_symbols: bool,
    pub content_precision: i64,
}

pub const MRC_MODES: &[&str] = &["off", "auto", "force"];
pub const COLOR_TARGETS: &[&str] = &["original", "rgb", "cmyk", "gray"];
pub const JPEG_SUBSAMPLING: &[&str] = &["auto", "444", "422", "420"];
pub const SPEEDS: &[&str] = &["quality", "balanced", "fast"];
pub const ANNOTATION_MODES: &[&str] = &["keep", "flatten", "remove"];
pub const PDF_VERSIONS: &[&str] = &["none", "1.4", "1.5", "1.6", "1.7", "2.0"];

fn js_round(v: f64) -> f64 {
    (v + 0.5).floor()
}

fn clamp(v: Option<&Value>, lo: i64, hi: i64, dflt: i64) -> i64 {
    let n = v.and_then(Value::as_f64).filter(|n| n.is_finite()).unwrap_or(dflt as f64);
    (js_round(n) as i64).clamp(lo, hi)
}

fn boolean(v: Option<&Value>, dflt: bool) -> bool {
    v.and_then(Value::as_bool).unwrap_or(dflt)
}

fn pick(values: &[&str], v: Option<&Value>, dflt: &str) -> String {
    let candidates: Vec<String> = match v {
        Some(Value::Number(n)) => match n.as_f64() {
            Some(f) if f.is_finite() => vec![format!("{f}"), format!("{f:.1}")],
            _ => vec![],
        },
        Some(Value::String(s)) => vec![s.clone()],
        _ => vec![],
    };
    candidates
        .into_iter()
        .find(|c| values.contains(&c.as_str()))
        .unwrap_or_else(|| dflt.to_string())
}

impl Options {
    pub fn normalize(raw: &Map<String, Value>) -> Options {
        let g = |k: &str| raw.get(k);
        let annotations = pick(ANNOTATION_MODES, g("annotations"), "keep");
        let annotations = if annotations == "keep" && g("removeAnnots") == Some(&Value::Bool(true)) {
            "flatten".to_string()
        } else {
            annotations
        };
        let precision = clamp(g("contentPrecision"), 0, 8, 0);
        Options {
            image_quality: clamp(g("imageQuality"), 5, 100, 80),
            max_dpi: clamp(g("maxDpi"), 0, 600, 150),
            max_dpi_color: clamp(g("maxDpiColor"), 0, 600, 0),
            max_dpi_gray: clamp(g("maxDpiGray"), 0, 600, 0),
            max_dpi_mono: clamp(g("maxDpiMono"), 0, 1200, 0),
            threshold_dpi: clamp(g("thresholdDpi"), 0, 2400, 0),
            color_target: pick(COLOR_TARGETS, g("colorTarget"), "original"),
            jpeg_subsampling: pick(JPEG_SUBSAMPLING, g("jpegSubsampling"), "auto"),
            jpeg_progressive: boolean(g("jpegProgressive"), false),
            speed: pick(SPEEDS, g("speed"), "balanced"),
            lossless: boolean(g("lossless"), false),
            grayscale: boolean(g("grayscale"), false),
            force_downsample: boolean(g("forceDownsample"), false),
            reduce_color: boolean(g("reduceColor"), false),
            clip_images: boolean(g("clipImages"), false),
            remove_alternates: boolean(g("removeAlternates"), false),
            flatten_icc: boolean(g("flattenIcc"), false),
            prefer_jpx: boolean(g("preferJpx"), false),
            subset_fonts: boolean(g("subsetFonts"), false),
            remove_standard_fonts: boolean(g("removeStandardFonts"), false),
            unembed_aliased_fonts: boolean(g("unembedAliasedFonts"), false),
            rasterize_pages: boolean(g("rasterizePages"), false),
            rasterize_dpi: clamp(g("rasterizeDpi"), 36, 600, 150),
            rasterize_quality: clamp(g("rasterizeQuality"), 1, 100, 50),
            rasterize_text: boolean(g("rasterizeText"), false),
            merge_fonts: boolean(g("mergeFonts"), false),
            annotations,
            remove_annots: boolean(g("removeAnnots"), false),
            flatten_forms: boolean(g("flattenForms"), false),
            flatten_links: boolean(g("flattenLinks"), false),
            remove_signatures: boolean(g("removeSignatures"), false),
            remove_thumbnails: boolean(g("removeThumbnails"), false),
            remove_app_data: boolean(g("removeAppData"), false),
            remove_struct_tree: boolean(g("removeStructTree"), false),
            remove_threads: boolean(g("removeThreads"), false),
            remove_spider_info: boolean(g("removeSpiderInfo"), false),
            remove_output_intents: boolean(g("removeOutputIntents"), false),
            remove_metadata: boolean(g("removeMetadata"), false),
            remove_bookmarks: boolean(g("removeBookmarks"), false),
            remove_attachments: boolean(g("removeAttachments"), false),
            remove_actions: boolean(g("removeActions"), false),
            remove_named_dests: boolean(g("removeNamedDests"), false),
            remove_print_settings: boolean(g("removePrintSettings"), false),
            remove_search_index: boolean(g("removeSearchIndex"), false),
            remove_signature_appearances: boolean(g("removeSignatureAppearances"), false),
            preserve_conformance: boolean(g("preserveConformance"), false),
            brotli: boolean(g("brotli"), false),
            linearize: boolean(g("linearize"), false),
            min_version: pick(PDF_VERSIONS, g("minVersion"), "none"),
            mrc: pick(MRC_MODES, g("mrc"), "off"),
            mrc_background_dpi: clamp(g("mrcBackgroundDpi"), 36, 300, 75),
            mrc_background_quality: clamp(g("mrcBackgroundQuality"), 5, 100, 30),
            mrc_foreground_quality: clamp(g("mrcForegroundQuality"), 5, 100, 50),
            mrc_detect_photos: boolean(g("mrcDetectPhotos"), true),
            jbig2_symbols: boolean(g("jbig2Symbols"), false),
            content_precision: if precision == 0 { 0 } else { precision.max(3) },
        }
    }

    pub fn to_map(&self) -> Map<String, Value> {
        match serde_json::to_value(self) {
            Ok(Value::Object(m)) => m,
            _ => Map::new(),
        }
    }

    pub fn with(&self, overrides: &Map<String, Value>) -> Options {
        let mut m = self.to_map();
        for (k, v) in overrides {
            m.insert(k.clone(), v.clone());
        }
        Options::normalize(&m)
    }

    pub fn tokens(&self) -> Vec<String> {
        let o = self;
        let mut t: Vec<String> = Vec::new();
        let mut mask: u32 = 0;
        if !o.lossless {
            let color = if o.max_dpi_color != 0 { o.max_dpi_color } else { o.max_dpi };
            let gray = if o.max_dpi_gray != 0 { o.max_dpi_gray } else { o.max_dpi };
            if o.max_dpi > 0 {
                t.push(format!("1={}", o.max_dpi));
            }
            if color != o.max_dpi {
                t.push(format!("57={color}"));
            }
            if gray != o.max_dpi {
                t.push(format!("58={gray}"));
            }
            if o.max_dpi_mono > 0 {
                t.push(format!("59={}", o.max_dpi_mono));
            }
            let limits: Vec<i64> = [color, gray, o.max_dpi_mono].into_iter().filter(|d| *d > 0).collect();
            if let Some(lowest) = limits.iter().min() {
                if o.threshold_dpi > 0 {
                    t.push(format!("50={}", o.threshold_dpi.max(*lowest)));
                } else if o.force_downsample {
                    t.push(format!("50={lowest}"));
                }
            }
        }
        if o.lossless {
            t.push("3=0".into());
        } else {
            t.push(format!("2={}", o.image_quality));
            t.push("3=4".into());
        }
        if !o.lossless && o.color_target != "original" {
            let id = match o.color_target.as_str() {
                "rgb" => 1,
                "cmyk" => 2,
                _ => 3,
            };
            t.push(format!("51={id}"));
            t.push("69=1".into());
        } else if !o.lossless && o.grayscale {
            t.push("51=3".into());
        }
        if !o.lossless && o.jpeg_subsampling != "auto" {
            let id = match o.jpeg_subsampling.as_str() {
                "420" => 1,
                "422" => 2,
                _ => 3,
            };
            t.push(format!("53={id}"));
        }
        if !o.lossless && o.jpeg_progressive {
            t.push("55=1".into());
        }
        if o.speed != "balanced" {
            t.push(format!("70={}", if o.speed == "quality" { 1 } else { 2 }));
        }
        if o.flatten_icc && !o.lossless {
            t.push("8=1".into());
        }
        if !o.lossless && o.prefer_jpx {
            t.push("18=1".into());
        }
        if o.reduce_color {
            t.push("7=1".into());
            if !o.lossless {
                t.push("63=10".into());
            }
        }
        if !o.lossless && o.clip_images {
            t.push("6=1".into());
        }
        if o.remove_alternates {
            mask |= 0x40;
        }
        t.push(format!("10={}", if o.subset_fonts { 1 } else { 0 }));
        if o.remove_standard_fonts {
            t.push("12=1".into());
        }
        if o.unembed_aliased_fonts {
            t.push("9=1".into());
        }
        if o.merge_fonts {
            t.extend(["13=1", "14=1", "16=1", "17=1"].map(String::from));
        }
        if o.annotations == "flatten" {
            mask |= 0x40000;
        }
        if o.annotations == "remove" {
            mask |= 0x800000;
        }
        for (on, bit) in [
            (o.flatten_forms, 0x100000),
            (o.flatten_links, 0x200000),
            (o.remove_signatures, 0x400000),
            (o.remove_thumbnails, 0x2),
            (o.remove_app_data, 0x80 | 0x2000),
            (o.remove_struct_tree, 0x1),
            (o.remove_threads, 0x8000),
            (o.remove_spider_info, 0x80000),
            (o.remove_output_intents, 0x4000),
            (o.remove_metadata, 0x100),
            (o.remove_bookmarks, 0x4),
            (o.remove_attachments, 0x20),
            (o.remove_actions, 0x1000),
            (o.remove_named_dests, 0x800),
            (o.remove_print_settings, 0x200),
            (o.remove_search_index, 0x400),
            (o.remove_signature_appearances, 0x10000),
        ] {
            if on {
                mask |= bit;
            }
        }
        if mask != 0 {
            t.push(format!("20={mask}"));
        }
        if o.rasterize_pages {
            t.push(format!("60={}", if o.rasterize_text { 2 } else { 1 }));
            t.push(format!("61={}", o.rasterize_dpi));
            t.push(format!("62={}", o.rasterize_quality));
        }
        if !o.lossless && o.mrc != "off" {
            t.push(format!("40={}", if o.mrc == "force" { 2 } else { 1 }));
            t.push(format!("42={}", o.mrc_background_dpi));
            t.push(format!("43={}", o.mrc_background_quality));
            t.push(format!("44={}", o.mrc_foreground_quality));
            if !o.mrc_detect_photos {
                t.push("72=0".into());
            }
        }
        if !o.lossless && o.jbig2_symbols {
            t.push("73=1".into());
        }
        if !o.lossless && o.content_precision > 0 {
            t.push(format!("67={}", o.content_precision));
        }
        if o.preserve_conformance {
            t.push("64=3".into());
        }
        if o.brotli && !o.preserve_conformance {
            t.push("65=1".into());
            t.push("66=11".into());
        }
        t
    }
}
