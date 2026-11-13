use crate::options::Options;

pub const QUALITY_FLOOR: i64 = 5;
pub const DPI_LADDER: [i64; 3] = [72, 50, 36];
pub const QUALITY_STEPS: u32 = 7;

pub fn start_quality(base: &Options) -> i64 {
    95.min(if base.image_quality > QUALITY_FLOOR { base.image_quality } else { 80 })
}

pub fn ladder(base: &Options) -> Vec<Options> {
    DPI_LADDER
        .iter()
        .map(|&dpi| {
            let mut o = base.clone();
            o.image_quality = QUALITY_FLOOR;
            o.max_dpi = dpi;
            o.max_dpi_color = 0;
            o.max_dpi_gray = 0;
            o.force_downsample = true;
            o
        })
        .collect()
}

pub fn miss_warning(smallest: u64, target: u64) -> String {
    format!(
        "target of {target} bytes not reached: {smallest} bytes is the smallest this document compresses to at quality {QUALITY_FLOOR} and {} dpi. For image-only documents, rasterizePages can go further.",
        DPI_LADDER[DPI_LADDER.len() - 1]
    )
}
