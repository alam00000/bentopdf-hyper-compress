export interface HyperCompressOptions {
  imageQuality: number;
  maxDpi: number;
  lossless: boolean;
  grayscale: boolean;
  forceDownsample: boolean;
  reduceColor: boolean;
  clipImages: boolean;
  removeAlternates: boolean;
  flattenIcc: boolean;
  preferJpx: boolean;
  subsetFonts: boolean;
  removeStandardFonts: boolean;
  unembedAliasedFonts: boolean;
  rasterizePages: boolean;
  rasterizeDpi: number;
  rasterizeQuality: number;
  mergeFonts: boolean;
  removeAnnots: boolean;
  flattenForms: boolean;
  flattenLinks: boolean;
  removeThumbnails: boolean;
  removeAppData: boolean;
  removeStructTree: boolean;
  removeThreads: boolean;
  removeSpiderInfo: boolean;
  removeOutputIntents: boolean;
  preserveConformance: boolean;
  brotli: boolean;
}

export function normalizeHyperOptions(raw: unknown): HyperCompressOptions {
  const a = (raw ?? {}) as Partial<Record<keyof HyperCompressOptions, unknown>>;
  const clamp = (n: unknown, lo: number, hi: number, dflt: number): number => {
    const v = typeof n === 'number' && Number.isFinite(n) ? n : dflt;
    return Math.max(lo, Math.min(hi, Math.round(v)));
  };
  const bool = (v: unknown, dflt: boolean): boolean =>
    typeof v === 'boolean' ? v : dflt;
  return {
    imageQuality: clamp(a.imageQuality, 5, 100, 80),
    maxDpi: clamp(a.maxDpi, 0, 600, 150),
    lossless: bool(a.lossless, false),
    grayscale: bool(a.grayscale, false),
    forceDownsample: bool(a.forceDownsample, false),
    reduceColor: bool(a.reduceColor, false),
    clipImages: bool(a.clipImages, false),
    removeAlternates: bool(a.removeAlternates, false),
    flattenIcc: bool(a.flattenIcc, false),
    preferJpx: bool(a.preferJpx, false),
    subsetFonts: bool(a.subsetFonts, false),
    removeStandardFonts: bool(a.removeStandardFonts, false),
    unembedAliasedFonts: bool(a.unembedAliasedFonts, false),
    rasterizePages: bool(a.rasterizePages, false),
    rasterizeDpi: clamp(a.rasterizeDpi, 36, 600, 150),
    rasterizeQuality: clamp(a.rasterizeQuality, 1, 100, 50),
    mergeFonts: bool(a.mergeFonts, false),
    removeAnnots: bool(a.removeAnnots, false),
    flattenForms: bool(a.flattenForms, false),
    flattenLinks: bool(a.flattenLinks, false),
    removeThumbnails: bool(a.removeThumbnails, false),
    removeAppData: bool(a.removeAppData, false),
    removeStructTree: bool(a.removeStructTree, false),
    removeThreads: bool(a.removeThreads, false),
    removeSpiderInfo: bool(a.removeSpiderInfo, false),
    removeOutputIntents: bool(a.removeOutputIntents, false),
    preserveConformance: bool(a.preserveConformance, false),
    brotli: bool(a.brotli, false),
  };
}

export function buildHyperTokens(o: HyperCompressOptions): string[] {
  const tokens: string[] = [];
  let mask = 0;
  if (!o.lossless && o.maxDpi > 0) {
    tokens.push(`1=${o.maxDpi}`);
    if (o.forceDownsample) tokens.push(`50=${o.maxDpi}`);
  }
  if (o.lossless) {
    tokens.push('3=0');
  } else {
    tokens.push(`2=${o.imageQuality}`);
    tokens.push('3=4');
  }
  if (!o.lossless && o.grayscale) tokens.push('51=3');
  if (o.flattenIcc) tokens.push('8=1');
  if (!o.lossless && o.preferJpx) tokens.push('18=1');
  if (o.reduceColor) {
    tokens.push('7=1');
    if (!o.lossless) tokens.push('63=10');
  }
  if (!o.lossless && o.clipImages) tokens.push('6=1');
  if (o.removeAlternates) mask |= 0x40;
  tokens.push(`10=${o.subsetFonts ? 1 : 0}`);
  if (o.removeStandardFonts) tokens.push('12=1');
  if (o.unembedAliasedFonts) tokens.push('9=1');
  if (o.mergeFonts) {
    tokens.push('13=1', '14=1', '16=1', '17=1');
  }
  if (o.removeAnnots) mask |= 0x40000;
  if (o.flattenForms) mask |= 0x100000;
  if (o.flattenLinks) mask |= 0x200000;
  if (o.removeThumbnails) mask |= 0x2;
  if (o.removeAppData) mask |= 0x80 | 0x2000;
  if (o.removeStructTree) mask |= 0x1;
  if (o.removeThreads) mask |= 0x8000;
  if (o.removeSpiderInfo) mask |= 0x80000;
  if (o.removeOutputIntents) mask |= 0x4000;
  if (mask) tokens.push(`20=${mask}`);
  if (o.rasterizePages) {
    tokens.push('60=1', `61=${o.rasterizeDpi}`, `62=${o.rasterizeQuality}`);
  }
  if (o.preserveConformance) tokens.push('64=3');
  if (o.brotli && !o.preserveConformance) tokens.push('65=1', '66=11');
  return tokens;
}

export type HyperOptionGroup =
  | 'Images'
  | 'Rasterization'
  | 'Fonts'
  | 'Structure and interactivity'
  | 'Metadata and cleanup'
  | 'Output format';

export interface HyperOptionDoc {
  group: HyperOptionGroup;
  type: 'boolean' | 'number';
  min?: number;
  max?: number;
  default: boolean | number;
  description: string;
}

export const HYPER_OPTION_DOCS: Record<keyof HyperCompressOptions, HyperOptionDoc> = {
  imageQuality: {
    group: 'Images', type: 'number', min: 5, max: 100, default: 80,
    description: 'JPEG quality for re-encoded images. Lower is smaller with more visible artefacts. Below about 20 the artefacts are obvious, which is why no preset goes there, but a size target can.',
  },
  maxDpi: {
    group: 'Images', type: 'number', min: 0, max: 600, default: 150,
    description: 'Images above this resolution are resampled down. 0 leaves resolution alone.',
  },
  forceDownsample: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Resample even when the image is only slightly above the limit.',
  },
  lossless: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Turns off quality, resolution and every other lossy image step.',
  },
  grayscale: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Convert images to grayscale.',
  },
  reduceColor: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Reduce colour complexity where it does not change appearance.',
  },
  clipImages: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Crop image data hidden outside the visible clip region.',
  },
  preferJpx: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Let JPEG 2000 compete with JPEG per image and keep the smaller one.',
  },
  removeAlternates: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Drop alternate image versions.',
  },
  flattenIcc: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Flatten ICC colour profiles.',
  },
  rasterizePages: {
    group: 'Rasterization', type: 'boolean', default: false,
    description: 'Replace each page with a single rendered image. Shrinks vector-heavy pages dramatically.',
  },
  rasterizeDpi: {
    group: 'Rasterization', type: 'number', min: 36, max: 600, default: 150,
    description: 'Render resolution. Higher keeps more detail.',
  },
  rasterizeQuality: {
    group: 'Rasterization', type: 'number', min: 1, max: 100, default: 50,
    description: 'JPEG quality for the rasterized pages.',
  },
  subsetFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Keep only the glyphs the document uses. Lossless by definition.',
  },
  removeStandardFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Unembed the standard 14 PDF fonts; viewers supply them.',
  },
  unembedAliasedFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Unembed metric-compatible clones of the standard fonts. Arial, Times New Roman and Courier New map to the built-in equivalents. Only applied when safe.',
  },
  mergeFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Merge duplicate font programs and dictionaries.',
  },
  removeAnnots: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove comments and other annotations.',
  },
  flattenForms: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Flatten form fields into page content. Form fields stop being fillable.',
  },
  flattenLinks: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Flatten link annotations. Links stop being clickable.',
  },
  removeStructTree: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Drop the structure tree. Removes tagging, which screen readers rely on.',
  },
  removeThreads: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove article threads.',
  },
  removeThumbnails: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove embedded page thumbnails.',
  },
  removeAppData: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove application-private data and piece info.',
  },
  removeSpiderInfo: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove web capture information.',
  },
  removeOutputIntents: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove output intents.',
  },
  preserveConformance: {
    group: 'Output format', type: 'boolean', default: false,
    description: 'Keep a PDF/A document conformant. Disables every step that would break the standard, including Brotli and rasterization; anything dropped is reported in warnings.',
  },
  brotli: {
    group: 'Output format', type: 'boolean', default: false,
    description: 'PDF 2.0 Brotli stream compression. Smaller files, but the reader must support it (MuPDF 1.26+, Ghostscript 10.06+, Firefox). Off while preserving PDF/A.',
  },
};
