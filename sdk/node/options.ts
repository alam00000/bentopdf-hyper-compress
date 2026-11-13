export interface HyperCompressOptions {
  imageQuality: number;
  maxDpi: number;
  maxDpiColor: number;
  maxDpiGray: number;
  maxDpiMono: number;
  thresholdDpi: number;
  colorTarget: HyperColorTarget;
  jpegSubsampling: HyperJpegSubsampling;
  jpegProgressive: boolean;
  speed: HyperSpeed;
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
  rasterizeText: boolean;
  mergeFonts: boolean;
  annotations: HyperAnnotationMode;
  removeAnnots: boolean;
  flattenForms: boolean;
  flattenLinks: boolean;
  removeSignatures: boolean;
  removeThumbnails: boolean;
  removeAppData: boolean;
  removeStructTree: boolean;
  removeThreads: boolean;
  removeSpiderInfo: boolean;
  removeOutputIntents: boolean;
  removeMetadata: boolean;
  removeBookmarks: boolean;
  removeAttachments: boolean;
  removeActions: boolean;
  removeNamedDests: boolean;
  removePrintSettings: boolean;
  removeSearchIndex: boolean;
  removeSignatureAppearances: boolean;
  preserveConformance: boolean;
  brotli: boolean;
  linearize: boolean;
  minVersion: HyperPdfVersion;
  mrc: HyperMrcMode;
  mrcBackgroundDpi: number;
  mrcBackgroundQuality: number;
  mrcForegroundQuality: number;
  mrcDetectPhotos: boolean;
  jbig2Symbols: boolean;
  contentPrecision: number;
}

export type HyperMrcMode = 'off' | 'auto' | 'force';
export type HyperColorTarget = 'original' | 'rgb' | 'cmyk' | 'gray';
export type HyperJpegSubsampling = 'auto' | '444' | '422' | '420';
export type HyperSpeed = 'quality' | 'balanced' | 'fast';
export type HyperAnnotationMode = 'keep' | 'flatten' | 'remove';
export type HyperPdfVersion = 'none' | '1.4' | '1.5' | '1.6' | '1.7' | '2.0';

const MRC_MODES: readonly HyperMrcMode[] = ['off', 'auto', 'force'];
const COLOR_TARGETS: readonly HyperColorTarget[] = ['original', 'rgb', 'cmyk', 'gray'];
const JPEG_SUBSAMPLING: readonly HyperJpegSubsampling[] = ['auto', '444', '422', '420'];
const SPEEDS: readonly HyperSpeed[] = ['quality', 'balanced', 'fast'];
const ANNOTATION_MODES: readonly HyperAnnotationMode[] = ['keep', 'flatten', 'remove'];
const PDF_VERSIONS: readonly HyperPdfVersion[] = ['none', '1.4', '1.5', '1.6', '1.7', '2.0'];

const COLOR_TARGET_IDS: Record<HyperColorTarget, number> = { original: 0, rgb: 1, cmyk: 2, gray: 3 };
const JPEG_SUBSAMPLING_IDS: Record<HyperJpegSubsampling, number> = { auto: 0, '420': 1, '422': 2, '444': 3 };
const SPEED_IDS: Record<HyperSpeed, number> = { balanced: 0, quality: 1, fast: 2 };

function pick<T extends string>(values: readonly T[], v: unknown, dflt: T): T {
  const candidates = typeof v === 'number' && Number.isFinite(v) ? [String(v), v.toFixed(1)] : [v];
  return (candidates.find((c) => values.includes(c as T)) as T | undefined) ?? dflt;
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
    maxDpiColor: clamp(a.maxDpiColor, 0, 600, 0),
    maxDpiGray: clamp(a.maxDpiGray, 0, 600, 0),
    maxDpiMono: clamp(a.maxDpiMono, 0, 1200, 0),
    thresholdDpi: clamp(a.thresholdDpi, 0, 2400, 0),
    colorTarget: pick(COLOR_TARGETS, a.colorTarget, 'original'),
    jpegSubsampling: pick(JPEG_SUBSAMPLING, a.jpegSubsampling, 'auto'),
    jpegProgressive: bool(a.jpegProgressive, false),
    speed: pick(SPEEDS, a.speed, 'balanced'),
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
    rasterizeText: bool(a.rasterizeText, false),
    mergeFonts: bool(a.mergeFonts, false),
    annotations: ((mode) => (mode === 'keep' && a.removeAnnots === true ? 'flatten' : mode))(
      pick(ANNOTATION_MODES, a.annotations, 'keep'),
    ),
    removeAnnots: bool(a.removeAnnots, false),
    flattenForms: bool(a.flattenForms, false),
    flattenLinks: bool(a.flattenLinks, false),
    removeSignatures: bool(a.removeSignatures, false),
    removeThumbnails: bool(a.removeThumbnails, false),
    removeAppData: bool(a.removeAppData, false),
    removeStructTree: bool(a.removeStructTree, false),
    removeThreads: bool(a.removeThreads, false),
    removeSpiderInfo: bool(a.removeSpiderInfo, false),
    removeOutputIntents: bool(a.removeOutputIntents, false),
    removeMetadata: bool(a.removeMetadata, false),
    removeBookmarks: bool(a.removeBookmarks, false),
    removeAttachments: bool(a.removeAttachments, false),
    removeActions: bool(a.removeActions, false),
    removeNamedDests: bool(a.removeNamedDests, false),
    removePrintSettings: bool(a.removePrintSettings, false),
    removeSearchIndex: bool(a.removeSearchIndex, false),
    removeSignatureAppearances: bool(a.removeSignatureAppearances, false),
    preserveConformance: bool(a.preserveConformance, false),
    brotli: bool(a.brotli, false),
    linearize: bool(a.linearize, false),
    minVersion: pick(PDF_VERSIONS, a.minVersion, 'none'),
    mrc: pick(MRC_MODES, a.mrc, 'off'),
    mrcBackgroundDpi: clamp(a.mrcBackgroundDpi, 36, 300, 75),
    mrcBackgroundQuality: clamp(a.mrcBackgroundQuality, 5, 100, 30),
    mrcForegroundQuality: clamp(a.mrcForegroundQuality, 5, 100, 50),
    mrcDetectPhotos: bool(a.mrcDetectPhotos, true),
    jbig2Symbols: bool(a.jbig2Symbols, false),
    contentPrecision: ((p) => (p === 0 ? 0 : Math.max(3, p)))(
      clamp(a.contentPrecision, 0, 8, 0),
    ),
  };
}

export function buildHyperTokens(o: HyperCompressOptions): string[] {
  const tokens: string[] = [];
  let mask = 0;
  if (!o.lossless) {
    const color = o.maxDpiColor || o.maxDpi;
    const gray = o.maxDpiGray || o.maxDpi;
    if (o.maxDpi > 0) tokens.push(`1=${o.maxDpi}`);
    if (color !== o.maxDpi) tokens.push(`57=${color}`);
    if (gray !== o.maxDpi) tokens.push(`58=${gray}`);
    if (o.maxDpiMono > 0) tokens.push(`59=${o.maxDpiMono}`);
    const limits = [color, gray, o.maxDpiMono].filter((d) => d > 0);
    if (limits.length > 0) {
      const lowest = Math.min(...limits);
      if (o.thresholdDpi > 0) tokens.push(`50=${Math.max(o.thresholdDpi, lowest)}`);
      else if (o.forceDownsample) tokens.push(`50=${lowest}`);
    }
  }
  if (o.lossless) {
    tokens.push('3=0');
  } else {
    tokens.push(`2=${o.imageQuality}`);
    tokens.push('3=4');
  }
  if (!o.lossless && o.colorTarget !== 'original') {
    tokens.push(`51=${COLOR_TARGET_IDS[o.colorTarget]}`, '69=1');
  } else if (!o.lossless && o.grayscale) {
    tokens.push('51=3');
  }
  if (!o.lossless && o.jpegSubsampling !== 'auto') {
    tokens.push(`53=${JPEG_SUBSAMPLING_IDS[o.jpegSubsampling]}`);
  }
  if (!o.lossless && o.jpegProgressive) tokens.push('55=1');
  if (o.speed !== 'balanced') tokens.push(`70=${SPEED_IDS[o.speed]}`);
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
  if (o.annotations === 'flatten') mask |= 0x40000;
  if (o.annotations === 'remove') mask |= 0x800000;
  if (o.flattenForms) mask |= 0x100000;
  if (o.flattenLinks) mask |= 0x200000;
  if (o.removeSignatures) mask |= 0x400000;
  if (o.removeThumbnails) mask |= 0x2;
  if (o.removeAppData) mask |= 0x80 | 0x2000;
  if (o.removeStructTree) mask |= 0x1;
  if (o.removeThreads) mask |= 0x8000;
  if (o.removeSpiderInfo) mask |= 0x80000;
  if (o.removeOutputIntents) mask |= 0x4000;
  if (o.removeMetadata) mask |= 0x100;
  if (o.removeBookmarks) mask |= 0x4;
  if (o.removeAttachments) mask |= 0x20;
  if (o.removeActions) mask |= 0x1000;
  if (o.removeNamedDests) mask |= 0x800;
  if (o.removePrintSettings) mask |= 0x200;
  if (o.removeSearchIndex) mask |= 0x400;
  if (o.removeSignatureAppearances) mask |= 0x10000;
  if (mask) tokens.push(`20=${mask}`);
  if (o.rasterizePages) {
    tokens.push(`60=${o.rasterizeText ? 2 : 1}`, `61=${o.rasterizeDpi}`, `62=${o.rasterizeQuality}`);
  }
  if (!o.lossless && o.mrc !== 'off') {
    tokens.push(
      `40=${o.mrc === 'force' ? 2 : 1}`,
      `42=${o.mrcBackgroundDpi}`,
      `43=${o.mrcBackgroundQuality}`,
      `44=${o.mrcForegroundQuality}`,
    );
    if (!o.mrcDetectPhotos) tokens.push('72=0');
  }
  if (!o.lossless && o.jbig2Symbols) tokens.push('73=1');
  if (!o.lossless && o.contentPrecision > 0) tokens.push(`67=${o.contentPrecision}`);
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
  type: 'boolean' | 'number' | 'enum';
  values?: readonly string[];
  min?: number;
  max?: number;
  default: boolean | number | string;
  description: string;
}

export const HYPER_OPTION_DOCS: Record<keyof HyperCompressOptions, HyperOptionDoc> = {
  imageQuality: {
    group: 'Images', type: 'number', min: 5, max: 100, default: 80,
    description: 'JPEG quality for re-encoded images. Lower is smaller with more visible artefacts. Below about 20 the artefacts are obvious, which is why no preset goes there, but a size target can.',
  },
  maxDpi: {
    group: 'Images', type: 'number', min: 0, max: 600, default: 150,
    description: 'Colour and grayscale images above this resolution are resampled down. 0 leaves resolution alone. Shorthand for maxDpiColor and maxDpiGray; 1-bit images have their own limit, maxDpiMono.',
  },
  maxDpiColor: {
    group: 'Images', type: 'number', min: 0, max: 600, default: 0,
    description: 'Resolution limit for colour images. 0 uses maxDpi.',
  },
  maxDpiGray: {
    group: 'Images', type: 'number', min: 0, max: 600, default: 0,
    description: 'Resolution limit for grayscale images. 0 uses maxDpi.',
  },
  maxDpiMono: {
    group: 'Images', type: 'number', min: 0, max: 1200, default: 0,
    description: 'Resolution limit for 1-bit (black and white) images such as text scans. 0 leaves them at full resolution, because 1-bit text loses legibility quickly; 300 is a safe value. Downsampled 1-bit images stay 1-bit.',
  },
  thresholdDpi: {
    group: 'Images', type: 'number', min: 0, max: 2400, default: 0,
    description: 'Only resample images above this resolution. 0 keeps the built-in rule: lossy images are resampled once they exceed the limit, lossless ones once they exceed it by half. Values below the limit act as the limit, so an image is never scaled up.',
  },
  forceDownsample: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Resample every image above the limit, even one only slightly above it. By default lossless images are left alone until they exceed the limit by half, because resampling them buys little.',
  },
  lossless: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Turns off quality, resolution, colour conversion and every other lossy image step. Images are only stored more efficiently, so every pixel stays the same.',
  },
  grayscale: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Convert images to grayscale. Page text and vector colours are left alone; use colorTarget gray to convert those too.',
  },
  colorTarget: {
    group: 'Images', type: 'enum', values: COLOR_TARGETS, default: 'original',
    description: 'Convert the document to one colour model. rgb and cmyk convert images and page text and vector colours through ICC profiles (sRGB and a generic CMYK profile); pure grays and pure black map to K-only in CMYK. gray converts images, text and vectors to grayscale. Spot colours (Separation and DeviceN), patterns and shadings are kept. An image is only converted when the result is not larger, and if the converted file ends up larger than the original, the original is returned with a warning. Ignored when lossless and while preserving PDF/A.',
  },
  jpegSubsampling: {
    group: 'Images', type: 'enum', values: JPEG_SUBSAMPLING, default: 'auto',
    description: 'Chroma subsampling for re-encoded JPEG images. auto lets the encoder decide (full colour resolution); 420 halves colour resolution both ways and is smallest; 422 halves it horizontally; 444 keeps it. Ignored when lossless.',
  },
  jpegProgressive: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Write progressive JPEG images, which appear gradually while loading over slow connections. Usually a little smaller too.',
  },
  speed: {
    group: 'Images', type: 'enum', values: SPEEDS, default: 'balanced',
    description: 'How many encodings are tried per image. fast encodes JPEG only, with no JPEG 2000 trial and no palette trials. balanced runs the trials the other options ask for. quality also tries JPEG 2000 (except in PDF/A) and lossy and lossless palettes for every image and keeps the smallest. Ignored when lossless.',
  },
  reduceColor: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Store images with few colours as palette (indexed) images, which are much smaller than full colour. Only images that look the same afterwards are converted.',
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
    description: 'Remove alternate versions of images, such as high resolution copies kept for printing. Viewers show the main image either way.',
  },
  flattenIcc: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Replace embedded ICC colour profiles of 2 KB or more on page content with the matching plain colour space (gray, RGB or CMYK). Saves the profile bytes; colours can shift slightly where the profile differed from the standard one.',
  },
  mrc: {
    group: 'Images', type: 'enum', values: MRC_MODES, default: 'off',
    description: 'Mixed raster content for scanned pages: each page image is split into a sharp 1-bit text mask, a low-resolution background and a low-resolution ink colour layer. auto applies it to pages made of one or two page-sized images and no visible text; force applies it to every page. Every rewritten page is rendered before and after and rolled back if it changed visibly. Ignored when lossless.',
  },
  mrcBackgroundDpi: {
    group: 'Images', type: 'number', min: 36, max: 300, default: 75,
    description: 'Resolution of the MRC background and ink colour layers. The text mask keeps the scan resolution, up to 300 dpi.',
  },
  mrcBackgroundQuality: {
    group: 'Images', type: 'number', min: 5, max: 100, default: 30,
    description: 'JPEG quality of the MRC background layer.',
  },
  mrcForegroundQuality: {
    group: 'Images', type: 'number', min: 5, max: 100, default: 50,
    description: 'JPEG quality of the MRC ink colour layer.',
  },
  mrcDetectPhotos: {
    group: 'Images', type: 'boolean', default: true,
    description: 'Find photographic regions on MRC pages and keep them out of the 1-bit text mask, so photos are stored as a separate image layer at the background resolution instead of being broken into black specks.',
  },
  jbig2Symbols: {
    group: 'Images', type: 'boolean', default: false,
    description: 'Store repeated shapes in black and white images once and reuse them across pages (lossy JBIG2). Smaller scans, but a shape can be swapped for a similar one, so a page is kept only when every small area matches the original. Off keeps black and white images exact.',
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
  rasterizeText: {
    group: 'Rasterization', type: 'boolean', default: false,
    description: 'Rasterize pages even when the document has a text layer. Text stops being selectable and searchable, and links stop working. Needs rasterizePages.',
  },
  subsetFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Keep only the glyphs the document uses. Lossless: the text looks and searches the same.',
  },
  removeStandardFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Unembed the standard 14 PDF fonts (Times, Helvetica, Courier, Symbol and ZapfDingbats in their styles); every viewer has them built in.',
  },
  unembedAliasedFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Unembed metric-compatible clones of the standard fonts. Arial, Times New Roman and Courier New map to the built-in equivalents. Only applied when safe.',
  },
  mergeFonts: {
    group: 'Fonts', type: 'boolean', default: false,
    description: 'Merge duplicate font programs and font dictionaries, and with them duplicate objects and resources, so each is stored once.',
  },
  annotations: {
    group: 'Structure and interactivity', type: 'enum', values: ANNOTATION_MODES, default: 'keep',
    description: 'What happens to comments, highlights, stamps and other markup annotations. keep leaves them; flatten draws them into the page so they can no longer be edited; remove deletes them. Form fields and links are never touched by this option; see flattenForms and flattenLinks.',
  },
  removeAnnots: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Deprecated, use annotations. true behaves like annotations flatten, which is what it always did.',
  },
  removeBookmarks: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove the bookmarks (outline) panel.',
  },
  removeActions: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove JavaScript and other actions that run code or open things: the open action, document, page and annotation triggers, document-level scripts, and JavaScript, launch, import, submit and media actions on links and bookmarks. Plain links and bookmark jumps keep working.',
  },
  removeNamedDests: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove the named destinations table. Links, bookmarks and the open action that point at a named destination are rewritten to point at the page directly first, so they keep working. Links from other documents to a name in this one stop working.',
  },
  removeAttachments: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove embedded files, both document attachments and file attachment annotations.',
  },
  removeSignatureAppearances: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove the visible appearance of empty (unsigned) signature fields. Signed documents are returned untouched unless removeSignatures is set.',
  },
  flattenForms: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Flatten form fields into page content. Form fields stop being fillable.',
  },
  flattenLinks: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Draw link annotations into the page. Links stop being clickable; any visible link border stays on the page.',
  },
  removeSignatures: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove digital signatures so a signed document can be compressed instead of returned untouched. The visible signature stays on the page, but the file is no longer signed and can be signed again.',
  },
  removeStructTree: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Drop the structure tree. Removes tagging, which screen readers rely on.',
  },
  removeThreads: {
    group: 'Structure and interactivity', type: 'boolean', default: false,
    description: 'Remove article threads, the reading order chains some magazines and newspapers use to jump between columns.',
  },
  removeThumbnails: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove embedded page thumbnails.',
  },
  removeAppData: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove application private data (PieceInfo) that programs like Illustrator store to reopen the file for editing. Viewers never use it.',
  },
  removeSpiderInfo: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove web capture information (SpiderInfo) left by tools that converted web pages to PDF.',
  },
  removeOutputIntents: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove output intents, which tell a printer which colour profile the document was prepared for. Needed for PDF/A and PDF/X; kept while preserving PDF/A.',
  },
  removeMetadata: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove the document information (title, author, producer, dates and custom entries) and the document XMP metadata packet. Refused with a warning while preserving PDF/A, which requires XMP.',
  },
  removePrintSettings: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove print presets (scaling, duplex, paper tray, page range, copies). Other viewer preferences stay.',
  },
  removeSearchIndex: {
    group: 'Metadata and cleanup', type: 'boolean', default: false,
    description: 'Remove web capture content identifiers (the IDS and URLS name trees).',
  },
  preserveConformance: {
    group: 'Output format', type: 'boolean', default: false,
    description: 'Keep a PDF/A document conformant. Disables every step that would break the standard, including Brotli and rasterization; anything dropped is reported in warnings.',
  },
  contentPrecision: {
    group: 'Output format', type: 'number', min: 0, max: 8, default: 0,
    description: 'Number precision when page content is rewritten compactly. 0 keeps every value exactly. 3 to 8 rounds coordinates to that many significant digits (always keeping at least 3 decimals, so no value moves by more than 0.0005 units) and colours to 3 decimals. Ignored when lossless.',
  },
  linearize: {
    group: 'Output format', type: 'boolean', default: false,
    description: 'Linearize the output (fast web view), so a browser can show the first page before the whole file has downloaded. Adds a little size. Skipped for signed documents, which are returned untouched, and while preserving PDF/A.',
  },
  minVersion: {
    group: 'Output format', type: 'enum', values: PDF_VERSIONS, default: 'none',
    description: 'Minimum PDF version of the output. A lower header version is raised; a higher one is never lowered. 2.0 is refused while preserving PDF/A-1, 2 or 3, which require a 1.x header.',
  },
  brotli: {
    group: 'Output format', type: 'boolean', default: false,
    description: 'PDF 2.0 Brotli stream compression. Smaller files, but the reader must support it (MuPDF 1.26+, Ghostscript 10.06+, Firefox). Off while preserving PDF/A.',
  },
};
