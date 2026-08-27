import type { HyperCompressOptions } from './options.js';

export const TARGET_QUALITY_FLOOR = 5;

export const TARGET_DPI_LADDER: readonly number[] = [72, 50, 36];

export const TARGET_QUALITY_STEPS = 7;

export function targetStartQuality(base: HyperCompressOptions): number {
  return Math.min(
    95,
    base.imageQuality > TARGET_QUALITY_FLOOR ? base.imageQuality : 80,
  );
}

export function targetLadder(base: HyperCompressOptions): HyperCompressOptions[] {
  return TARGET_DPI_LADDER.map((dpi) => ({
    ...base,
    imageQuality: TARGET_QUALITY_FLOOR,
    maxDpi: dpi,
    forceDownsample: true,
  }));
}

export function targetMissWarning(smallest: number, target: number): string {
  const floorDpi = TARGET_DPI_LADDER[TARGET_DPI_LADDER.length - 1];
  return (
    `target of ${target} bytes not reached: ${smallest} bytes is the smallest ` +
    `this document compresses to at quality ${TARGET_QUALITY_FLOOR} and ${floorDpi} dpi. ` +
    'For image-only documents, rasterizePages can go further.'
  );
}
