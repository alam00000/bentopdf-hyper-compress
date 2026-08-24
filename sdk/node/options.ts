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
    imageQuality: clamp(a.imageQuality, 20, 100, 80),
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
  if (o.preserveConformance) tokens.push('64=3');
  if (o.brotli && !o.preserveConformance) tokens.push('65=1', '66=11');
  return tokens;
}
