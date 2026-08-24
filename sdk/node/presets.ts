import type { HyperCompressOptions } from './options.js';

export type CompressLevel = 'low' | 'medium' | 'high' | 'lossless';

export const HYPER_PRESETS: Record<CompressLevel, HyperCompressOptions> = {
  low: {
    imageQuality: 80, maxDpi: 200, lossless: false, grayscale: false,
    forceDownsample: false, reduceColor: true, clipImages: false,
    removeAlternates: false, flattenIcc: true, preferJpx: true,
    subsetFonts: true, removeStandardFonts: false, unembedAliasedFonts: false,
    mergeFonts: true, removeAnnots: false, flattenForms: false,
    flattenLinks: false, removeThumbnails: true, removeAppData: true,
    removeStructTree: false, removeThreads: false, removeSpiderInfo: false,
    removeOutputIntents: false,
    preserveConformance: false, brotli: false,
  },
  medium: {
    imageQuality: 50, maxDpi: 150, lossless: false, grayscale: false,
    forceDownsample: false, reduceColor: true, clipImages: true,
    removeAlternates: false, flattenIcc: true, preferJpx: true,
    subsetFonts: true, removeStandardFonts: false, unembedAliasedFonts: false,
    mergeFonts: true, removeAnnots: false, flattenForms: false,
    flattenLinks: false, removeThumbnails: true, removeAppData: true,
    removeStructTree: false, removeThreads: false, removeSpiderInfo: false,
    removeOutputIntents: false,
    preserveConformance: false, brotli: false,
  },
  high: {
    imageQuality: 20, maxDpi: 72, lossless: false, grayscale: false,
    forceDownsample: false, reduceColor: true, clipImages: true,
    removeAlternates: true, flattenIcc: true, preferJpx: true,
    subsetFonts: true, removeStandardFonts: true, unembedAliasedFonts: true,
    mergeFonts: true, removeAnnots: false, flattenForms: false,
    flattenLinks: false, removeThumbnails: true, removeAppData: true,
    removeStructTree: false, removeThreads: true, removeSpiderInfo: true,
    removeOutputIntents: false,
    preserveConformance: false, brotli: false,
  },
  lossless: {
    imageQuality: 100, maxDpi: 0, lossless: true, grayscale: false,
    forceDownsample: false, reduceColor: true, clipImages: false,
    removeAlternates: false, flattenIcc: false, preferJpx: false,
    subsetFonts: true, removeStandardFonts: false, unembedAliasedFonts: false,
    mergeFonts: true, removeAnnots: false, flattenForms: false,
    flattenLinks: false, removeThumbnails: true, removeAppData: true,
    removeStructTree: false, removeThreads: false, removeSpiderInfo: false,
    removeOutputIntents: false,
    preserveConformance: false, brotli: false,
  },
};
