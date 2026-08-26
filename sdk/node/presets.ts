import type { HyperCompressOptions } from './options.js';

export type CompressLevel = 'low' | 'medium' | 'high' | 'lossless';

const PRESET_BASE: HyperCompressOptions = {
  imageQuality: 50, maxDpi: 150, lossless: false, grayscale: false,
  forceDownsample: false, reduceColor: true, clipImages: true,
  removeAlternates: false, flattenIcc: true, preferJpx: true,
  subsetFonts: true, removeStandardFonts: false, unembedAliasedFonts: false,
  rasterizePages: false, rasterizeDpi: 150, rasterizeQuality: 50,
  mergeFonts: true, removeAnnots: false, flattenForms: false,
  flattenLinks: false, removeThumbnails: true, removeAppData: true,
  removeStructTree: false, removeThreads: false, removeSpiderInfo: false,
  removeOutputIntents: false,
  preserveConformance: false, brotli: false,
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
    flattenIcc: false, preferJpx: false,
  },
};
