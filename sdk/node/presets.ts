import type { HyperCompressOptions } from './options.js';

export type CompressLevel = 'low' | 'medium' | 'high' | 'lossless';

const PRESET_BASE: HyperCompressOptions = {
  imageQuality: 50, maxDpi: 150, maxDpiColor: 0, maxDpiGray: 0, maxDpiMono: 0,
  thresholdDpi: 0, colorTarget: 'original', jpegSubsampling: 'auto',
  jpegProgressive: false, speed: 'balanced', lossless: false, grayscale: false,
  forceDownsample: false, reduceColor: true, clipImages: true,
  removeAlternates: false, flattenIcc: true, preferJpx: true,
  subsetFonts: true, removeStandardFonts: false, unembedAliasedFonts: false,
  rasterizePages: false, rasterizeDpi: 150, rasterizeQuality: 50, rasterizeText: false,
  mergeFonts: true, annotations: 'keep', removeAnnots: false, flattenForms: false,
  flattenLinks: false, removeSignatures: false, removeThumbnails: true, removeAppData: true,
  removeStructTree: false, removeThreads: false, removeSpiderInfo: false,
  removeOutputIntents: false, removeMetadata: false, removeBookmarks: false,
  removeAttachments: false, removeActions: false, removeNamedDests: false,
  removePrintSettings: false, removeSearchIndex: false, removeSignatureAppearances: false,
  preserveConformance: false, brotli: false, linearize: false, minVersion: 'none',
  mrc: 'off', mrcBackgroundDpi: 75, mrcBackgroundQuality: 30, mrcForegroundQuality: 50,
  mrcDetectPhotos: true,
  contentPrecision: 6,
};

export const HYPER_PRESETS: Record<CompressLevel, HyperCompressOptions> = {
  low: {
    ...PRESET_BASE,
    imageQuality: 80, maxDpi: 200, clipImages: false,
  },
  medium: {
    ...PRESET_BASE,
  },
  high: {
    ...PRESET_BASE,
    imageQuality: 20, maxDpi: 72, removeAlternates: true,
    removeStandardFonts: true, unembedAliasedFonts: true,
    removeThreads: true, removeSpiderInfo: true,
  },
  lossless: {
    ...PRESET_BASE,
    imageQuality: 100, maxDpi: 0, lossless: true, clipImages: false,
    flattenIcc: false, preferJpx: false, contentPrecision: 0,
  },
};
