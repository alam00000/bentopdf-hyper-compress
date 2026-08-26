import { spawn } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { buildHyperTokens, normalizeHyperOptions, type HyperCompressOptions } from './options.js';
import { HYPER_PRESETS, type CompressLevel } from './presets.js';

import { HyperError } from './errors.js';
export { HyperError, type HyperErrorCode } from './errors.js';
import {
  conformanceSafeOptions, detectPdfaInBytes, packAllowedWhilePreserving,
  restoreHeaderVersion, type PdfaLevel,
} from './pdfa.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const EXE = process.platform === 'win32' ? '.exe' : '';

function findPrebuilt(): string | null {
  let dir = HERE;
  for (let i = 0; i < 8; i++) {
    const candidate = path.join(dir, 'cli', 'prebuilt');
    if (fs.existsSync(path.join(candidate, 'hpdf-worker' + EXE))) return candidate;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

const PREBUILT = findPrebuilt();

const requireFromHere = createRequire(import.meta.url);
const packageBinCache = new Map<string, string | null>();

function platformPackageBin(name: string): string | null {
  const cached = packageBinCache.get(name);
  if (cached !== undefined) return cached;
  const pkg = `hyper-compress-${process.platform}-${process.arch}`;
  let bin: string | null;
  try {
    const dir = path.dirname(requireFromHere.resolve(`${pkg}/package.json`));
    const candidate = path.join(dir, 'bin', name + EXE);
    bin = fs.existsSync(candidate) ? candidate : null;
  } catch {
    bin = null;
  }
  packageBinCache.set(name, bin);
  return bin;
}

function tryResolveBin(envVar: string, name: string): string | null {
  const fromEnv = process.env[envVar];
  if (fromEnv) return fromEnv;
  if (PREBUILT) {
    const local = path.join(PREBUILT, name + EXE);
    if (fs.existsSync(local)) return local;
  }
  return platformPackageBin(name);
}

function requireBin(envVar: string, name: string): string {
  const bin = tryResolveBin(envVar, name);
  if (bin) return bin;
  throw new HyperError(
    'engine_error',
    `no ${name} binary for ${process.platform}-${process.arch}: use a platform with a native package (darwin-arm64, linux-x64, win32-x64), set ${envVar} to a binary you built (see BUILDING.md), or switch to the hyper-compress-wasm package`,
  );
}

export interface CompressInput {
  sourcePath: string;
  savePath?: string | undefined;
  password?: string | null | undefined;
  preset?: CompressLevel | undefined;
  options?: Partial<HyperCompressOptions> | undefined;
  targetSizeBytes?: number | undefined;
  signal?: AbortSignal | undefined;
  timeoutMs?: number | undefined;
}

export interface CompressResult {
  outputPath: string;
  originalSize: number;
  compressedSize: number;
  signed: boolean;
  pdfa: PdfaLevel | null;
  pdfaPreserved: boolean;
  metTarget: boolean | null;
  warnings: string[];
}

interface StageLimits {
  signal?: AbortSignal | undefined;
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
  return requireBin('HYPER_DRV', 'hpdf-worker');
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
): Promise<{ ok: boolean; signed: boolean; note: string | null }> {
  return new Promise((resolve) => {
    const child = spawn(driverPath(), [inPath, outPath, ...tokens], {
      stdio: ['ignore', 'ignore', 'pipe'],
      ...stageSpawnOpts(limits),
    });
    let stderr = '';
    child.stderr?.on('data', (chunk: Buffer) => {
      stderr += chunk.toString();
    });
    child.on('error', () => resolve({ ok: false, signed: false, note: null }));
    child.on('close', (code) => {
      const signed = /SIGNED_SKIP/.test(stderr);
      const note = stderr.match(/\[hyper\] (rasterize skipped:[^\r\n]*)/)?.[1] ?? null;
      resolve({ ok: code === 0, signed, note });
    });
  });
}

function runQpdf(args: string[], limits?: StageLimits): Promise<boolean> {
  const bin = tryResolveBin('HYPER_QPDF', 'qpdf');
  if (!bin) return Promise.resolve(false);
  return new Promise((resolve) => {
    const child = spawn(bin, args, { stdio: ['ignore', 'ignore', 'ignore'], ...stageSpawnOpts(limits) });
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
  requireBin('HYPER_QPDF', 'qpdf');
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
  } catch (err) {
    if (err instanceof HyperError && err.code !== 'decrypt_failed') throw err;
    return false;
  } finally {
    await fs.promises.rm(tmpDir, { recursive: true, force: true }).catch(() => {});
  }
}

export async function compress(input: CompressInput): Promise<CompressResult> {
  if (!input.sourcePath) throw new HyperError('engine_error', 'sourcePath is required');
  const requested = resolveOptions(input);
  let options = requested;
  const warnings: string[] = [];
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
    if (requested.rasterizePages && !options.rasterizePages) {
      warnings.push('rasterizePages disabled to preserve PDF/A conformance');
    }
    if (requested.brotli) {
      warnings.push('brotli disabled to preserve PDF/A conformance');
    }
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

    const { ok, signed, note } = await runDriver(workIn, drvOut, tokens, limits);
    if (note) warnings.push(note);

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
        warnings,
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
      warnings,
    };
  } finally {
    try {
      await fs.promises.rm(tmpDir, { recursive: true, force: true });
    } catch {
      void 0;
    }
  }
}
