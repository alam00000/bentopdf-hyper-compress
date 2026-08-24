import { spawn } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildHyperTokens, normalizeHyperOptions, type HyperCompressOptions } from './options.js';
import { HYPER_PRESETS, type CompressLevel } from './presets.js';

export type HyperErrorCode =
  | 'decrypt_failed'
  | 'engine_error'
  | 'timeout'
  | 'cancelled';

export class HyperError extends Error {
  readonly code: HyperErrorCode;
  constructor(code: HyperErrorCode, message?: string) {
    super(message ?? code);
    this.name = 'HyperError';
    this.code = code;
  }
}
import {
  conformanceSafeOptions, detectPdfaInBytes, packAllowedWhilePreserving,
  restoreHeaderVersion, type PdfaLevel,
} from './pdfa.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));

function findPrebuilt(): string {
  let dir = HERE;
  for (let i = 0; i < 8; i++) {
    const candidate = path.join(dir, 'cli', 'prebuilt');
    if (fs.existsSync(path.join(candidate, 'hpdf-worker'))) return candidate;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return path.resolve(HERE, '..', '..', 'cli', 'prebuilt');
}

const PREBUILT = findPrebuilt();

export interface CompressInput {
  sourcePath: string;
  savePath?: string;
  password?: string | null;
  preset?: CompressLevel;
  options?: Partial<HyperCompressOptions>;
  targetSizeBytes?: number;
  signal?: AbortSignal;
  timeoutMs?: number;
}

export interface CompressResult {
  outputPath: string;
  originalSize: number;
  compressedSize: number;
  signed: boolean;
  pdfa: PdfaLevel | null;
  pdfaPreserved: boolean;
  metTarget: boolean | null;
}

interface StageLimits {
  signal?: AbortSignal;
  timeoutMs: number;
}

const DEFAULT_STAGE_TIMEOUT_MS = 600_000;

function stageSpawnOpts(limits?: StageLimits): {
  signal?: AbortSignal;
  timeout?: number;
  killSignal: 'SIGKILL';
} {
  return {
    ...(limits?.signal ? { signal: limits.signal } : {}),
    timeout: limits?.timeoutMs ?? DEFAULT_STAGE_TIMEOUT_MS,
    killSignal: 'SIGKILL',
  };
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) throw new HyperError('cancelled');
}

function driverPath(): string {
  return process.env.HYPER_DRV ?? path.join(PREBUILT, 'hpdf-worker');
}

function qpdfPath(): string {
  return process.env.HYPER_QPDF ?? path.join(PREBUILT, 'qpdf');
}

async function statSize(p: string): Promise<number> {
  try {
    return (await fs.promises.stat(p)).size;
  } catch {
    return 0;
  }
}

function runDriver(
  inPath: string,
  outPath: string,
  tokens: string[],
  limits?: StageLimits,
): Promise<{ ok: boolean; signed: boolean }> {
  return new Promise((resolve) => {
    const child = spawn(driverPath(), [inPath, outPath, ...tokens], {
      stdio: ['ignore', 'ignore', 'pipe'],
      ...stageSpawnOpts(limits),
    });
    let stderr = '';
    child.stderr?.on('data', (chunk: Buffer) => {
      stderr += chunk.toString();
    });
    child.on('error', () => resolve({ ok: false, signed: false }));
    child.on('close', (code) => {
      const signed = /SIGNED_SKIP/.test(stderr);
      resolve({ ok: code === 0, signed });
    });
  });
}

function runQpdf(args: string[], limits?: StageLimits): Promise<boolean> {
  return new Promise((resolve) => {
    const child = spawn(qpdfPath(), args, { stdio: ['ignore', 'ignore', 'ignore'], ...stageSpawnOpts(limits) });
    child.on('error', () => resolve(false));
    child.on('close', (code) => resolve(code === 0));
  });
}

function runQpdfPack(inPath: string, outPath: string, limits?: StageLimits): Promise<boolean> {
  return runQpdf([
    '--warning-exit-0',
    '--object-streams=generate',
    '--recompress-flate',
    '--compression-level=9',
    inPath,
    outPath,
  ], limits);
}

async function runQpdfDecrypt(inPath: string, outPath: string, password: string, limits?: StageLimits): Promise<void> {
  const ok = await runQpdf(['--warning-exit-0', `--password=${password}`, '--decrypt', inPath, outPath], limits);
  if (!ok) throw new HyperError('decrypt_failed');
}

export function resolveOptions(input: CompressInput): HyperCompressOptions {
  const base = input.preset ? HYPER_PRESETS[input.preset] : HYPER_PRESETS.medium;
  return normalizeHyperOptions({ ...base, ...(input.options ?? {}) });
}

export async function verifyPassword(sourcePath: string, password: string, timeoutMs?: number): Promise<boolean> {
  const tmpDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'hyper-verify-'));
  try {
    await runQpdfDecrypt(sourcePath, path.join(tmpDir, 'dec.pdf'), password, {
      timeoutMs: timeoutMs ?? 60_000,
    });
    return true;
  } catch {
    return false;
  } finally {
    await fs.promises.rm(tmpDir, { recursive: true, force: true }).catch(() => {});
  }
}

export async function compress(input: CompressInput): Promise<CompressResult> {
  if (!input.sourcePath) throw new HyperError('engine_error', 'sourcePath is required');
  let options = resolveOptions(input);
  const password =
    typeof input.password === 'string' && input.password.length > 0 ? input.password : null;
  const originalSize = (await fs.promises.stat(input.sourcePath)).size;
  const limits: StageLimits = {
    signal: input.signal,
    timeoutMs: input.timeoutMs ?? DEFAULT_STAGE_TIMEOUT_MS,
  };

  const claimed = detectPdfaInBytes(await fs.promises.readFile(input.sourcePath));
  const preserving = Boolean(options.preserveConformance && claimed);
  if (preserving && claimed) {
    options = conformanceSafeOptions(options, claimed);
  }
  options = { ...options, preserveConformance: preserving };
  const tokens = buildHyperTokens(options);

  let finalPath: string;
  if (input.savePath && input.savePath.length > 0) {
    finalPath = input.savePath;
  } else {
    const dir = path.dirname(input.sourcePath);
    const ext = path.extname(input.sourcePath);
    const base = path.basename(input.sourcePath, ext);
    finalPath = path.join(dir, `${base}-compressed${ext || '.pdf'}`);
  }

  const tmpDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'hyper-'));
  const drvOut = path.join(tmpDir, 'drv.pdf');
  const packOut = path.join(tmpDir, 'pack.pdf');

  try {
    let workIn = input.sourcePath;
    let fallbackBase = input.sourcePath;
    if (password) {
      const dec = path.join(tmpDir, 'dec.pdf');
      await runQpdfDecrypt(input.sourcePath, dec, password, limits);
      workIn = dec;
      fallbackBase = dec;
    }
    const baselineSize = await statSize(fallbackBase);

    const skipPack = preserving && !packAllowedWhilePreserving();

    const runAttempt = async (
      attemptOptions: HyperCompressOptions,
      drvPath: string,
      packPath: string,
    ): Promise<{ ok: boolean; signed: boolean; candidate: string | null }> => {
      const attemptTokens = buildHyperTokens(attemptOptions);
      const r = await runDriver(workIn, drvPath, attemptTokens, limits);
      if (!r.ok || r.signed) return { ...r, candidate: null };
      if (skipPack) return { ...r, candidate: drvPath };
      if ((await runQpdfPack(drvPath, packPath, limits)) && (await statSize(packPath)) > 0) {
        return { ...r, candidate: packPath };
      }
      return { ...r, candidate: drvPath };
    };

    const target = input.targetSizeBytes && input.targetSizeBytes > 0
      ? Math.floor(input.targetSizeBytes)
      : null;

    const { ok, signed } = await runDriver(workIn, drvOut, tokens, limits);

    if (ok && signed) {
      await fs.promises.copyFile(fallbackBase, finalPath);
      return {
        outputPath: finalPath,
        originalSize,
        compressedSize: await statSize(finalPath),
        signed: true,
        pdfa: claimed,
        pdfaPreserved: Boolean(claimed),
        metTarget: target != null ? (await statSize(finalPath)) <= target : null,
      };
    }

    let packed: string | null = null;
    if (ok) {
      if (skipPack) {
        packed = drvOut;
      } else if ((await runQpdfPack(drvOut, packOut, limits)) && (await statSize(packOut)) > 0) {
        packed = packOut;
      } else {
        packed = drvOut;
      }
    } else if (!skipPack && (await runQpdfPack(workIn, packOut, limits)) &&
               (await statSize(packOut)) > 0) {
      packed = packOut;
    }

    if (target != null && packed && !preserving) {
      let bestFit: { path: string; size: number } | null = null;
      let smallest = { path: packed, size: await statSize(packed) };
      if (smallest.size <= target) {
        bestFit = smallest;
      } else {
        const attemptDrv = path.join(tmpDir, 'tgt-drv.pdf');
        const attemptPack = path.join(tmpDir, 'tgt-pack.pdf');
        const keepFit = path.join(tmpDir, 'tgt-fit.pdf');
        const keepSmallest = path.join(tmpDir, 'tgt-small.pdf');
        const baseOpts: HyperCompressOptions = {
          ...options,
          lossless: false,
        };
        const startQ = Math.min(
          95,
          baseOpts.imageQuality > 20 ? baseOpts.imageQuality : 80,
        );
        let lo = 20;
        let hi = startQ - 1;
        let iterations = 0;
        while (lo <= hi && iterations < 6) {
          iterations++;
          throwIfAborted(input.signal);
          const q = Math.floor((lo + hi) / 2);
          const res = await runAttempt(
            { ...baseOpts, imageQuality: q },
            attemptDrv,
            attemptPack,
          );
          if (!res.candidate) break;
          const size = await statSize(res.candidate);
          if (size > 0 && size < smallest.size) {
            await fs.promises.copyFile(res.candidate, keepSmallest);
            smallest = { path: keepSmallest, size };
          }
          if (size > 0 && size <= target) {
            await fs.promises.copyFile(res.candidate, keepFit);
            bestFit = { path: keepFit, size };
            lo = q + 1;
          } else {
            hi = q - 1;
          }
        }
        if (!bestFit) {
          const res = await runAttempt(
            {
              ...baseOpts,
              imageQuality: 20,
              maxDpi: 72,
              forceDownsample: true,
            },
            attemptDrv,
            attemptPack,
          );
          if (res.candidate) {
            const size = await statSize(res.candidate);
            if (size > 0 && size <= target) {
              await fs.promises.copyFile(res.candidate, keepFit);
              bestFit = { path: keepFit, size };
            } else if (size > 0 && size < smallest.size) {
              await fs.promises.copyFile(res.candidate, keepSmallest);
              smallest = { path: keepSmallest, size };
            }
          }
        }
        packed = bestFit ? bestFit.path : smallest.path;
      }
    }

    if (packed) {
      const packedSize = await statSize(packed);
      if (packedSize > 0 && packedSize < baselineSize) {
        await fs.promises.copyFile(packed, finalPath);
      } else {
        await fs.promises.copyFile(fallbackBase, finalPath);
      }
    } else {
      await fs.promises.copyFile(fallbackBase, finalPath);
    }

    if (preserving) {
      const src = await fs.promises.readFile(fallbackBase);
      const outBytes = await fs.promises.readFile(finalPath);
      const patched = restoreHeaderVersion(src, outBytes);
      if (patched !== outBytes) await fs.promises.writeFile(finalPath, patched);
    }

    return {
      outputPath: finalPath,
      originalSize,
      compressedSize: await statSize(finalPath),
      signed: false,
      pdfa: claimed,
      pdfaPreserved: preserving,
      metTarget: target != null ? (await statSize(finalPath)) <= target : null,
    };
  } finally {
    try {
      await fs.promises.rm(tmpDir, { recursive: true, force: true });
    } catch {
      void 0;
    }
  }
}
