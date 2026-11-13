use serde_json::{json, Map, Value};

use crate::options::Options;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Preset {
    Low,
    Medium,
    High,
    Lossless,
}

impl Preset {
    pub const ALL: [Preset; 4] = [Preset::Low, Preset::Medium, Preset::High, Preset::Lossless];

    pub fn name(self) -> &'static str {
        match self {
            Preset::Low => "low",
            Preset::Medium => "medium",
            Preset::High => "high",
            Preset::Lossless => "lossless",
        }
    }

    pub fn parse(s: &str) -> Option<Preset> {
        Preset::ALL.into_iter().find(|p| p.name() == s)
    }

    pub fn options(self) -> Options {
        let mut m = base();
        let changes = match self {
            Preset::Low => json!({ "imageQuality": 80, "maxDpi": 200, "clipImages": false }),
            Preset::Medium => json!({}),
            Preset::High => json!({
                "imageQuality": 20, "maxDpi": 72, "maxDpiMono": 150, "removeAlternates": true,
                "removeStandardFonts": true, "unembedAliasedFonts": true,
                "removeThreads": true, "removeSpiderInfo": true,
            }),
            Preset::Lossless => json!({
                "imageQuality": 100, "maxDpi": 0, "lossless": true, "clipImages": false,
                "flattenIcc": false, "preferJpx": false, "contentPrecision": 0,
            }),
        };
        if let Value::Object(c) = changes {
            m.extend(c);
        }
        Options::normalize(&m)
    }
}

fn base() -> Map<String, Value> {
    match json!({
        "imageQuality": 50, "maxDpi": 150, "maxDpiColor": 0, "maxDpiGray": 0, "maxDpiMono": 0,
        "thresholdDpi": 0, "colorTarget": "original", "jpegSubsampling": "auto",
        "jpegProgressive": false, "speed": "balanced", "lossless": false, "grayscale": false,
        "forceDownsample": false, "reduceColor": true, "clipImages": true,
        "removeAlternates": false, "flattenIcc": true, "preferJpx": true,
        "subsetFonts": true, "removeStandardFonts": false, "unembedAliasedFonts": false,
        "rasterizePages": false, "rasterizeDpi": 150, "rasterizeQuality": 50, "rasterizeText": false,
        "mergeFonts": true, "annotations": "keep", "removeAnnots": false, "flattenForms": false,
        "flattenLinks": false, "removeSignatures": false, "removeThumbnails": true, "removeAppData": true,
        "removeStructTree": false, "removeThreads": false, "removeSpiderInfo": false,
        "removeOutputIntents": false, "removeMetadata": false, "removeBookmarks": false,
        "removeAttachments": false, "removeActions": false, "removeNamedDests": false,
        "removePrintSettings": false, "removeSearchIndex": false, "removeSignatureAppearances": false,
        "preserveConformance": false, "brotli": false, "linearize": false, "minVersion": "none",
        "mrc": "off", "mrcBackgroundDpi": 75, "mrcBackgroundQuality": 30, "mrcForegroundQuality": 50,
        "mrcDetectPhotos": true, "jbig2Symbols": false,
        "contentPrecision": 6,
    }) {
        Value::Object(m) => m,
        _ => Map::new(),
    }
}
