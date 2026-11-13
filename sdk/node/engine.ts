import * as fs from 'node:fs';
import * as path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { normalizeHyperOptions, type HyperCompressOptions } from './options.js';
import { HYPER_PRESETS, type CompressLevel } from './presets.js';
import { HyperError, type HyperErrorCode } from './errors.js';
import type { PdfaLevel } from './pdfa.js';
import type { HyperReport } from './report.js';

export { HyperError, type HyperErrorCode } from './errors.js';
export type { HyperReport } from './report.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const EXE = process.platform === 'win32' ? '.exe' : '';
const ADDON = 'hyper-compress.node';

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

function platformPackageBin(file: string): string | null {
  const cached = packageBinCache.get(file);
  if (cached !== undefined) return cached;
  const pkg = `hyper-compress-${process.platform}-${process.arch}`;
  let bin: string | null;
  try {
    const dir = path.dirname(requireFromHere.resolve(`${pkg}/package.json`));
    const candidate = path.join(dir, 'bin', file);
    bin = fs.existsSync(candidate) ? candidate : null;
  } catch {
    bin = null;
  }
  packageBinCache.set(file, bin);
  return bin;
}

function tryResolve(envVar: string, file: string): string | null {
  const fromEnv = process.env[envVar];
  if (fromEnv) return fromEnv;
  if (PREBUILT) {
    const local = path.join(PREBUILT, file);
    if (fs.existsSync(local)) return local;
  }
  return platformPackageBin(file);
}

function missing(name: string, envVar: string): HyperError {
  return new HyperError(
    'engine_error',
    `no ${name} binary for ${process.platform}-${process.arch}: use a platform with a native package (darwin-arm64, linux-x64, linux-arm64, win32-x64), set ${envVar} to a binary you built (see BUILDING.md), or switch to the hyper-compress-wasm package`,
  );
}

interface CancelHandle {
  cancel(): void;
}

interface NativeAddon {
  compressNative(input: object, cancel?: CancelHandle): Promise<string>;
  verifyPasswordNative(input: object): Promise<string>;
  resolveOptionsNative(preset: string | null | undefined, options: object | null | undefined): HyperCompressOptions;
  buildTokensNative(options: object): string[];
  engineVersion(): string;
  CancelHandle: new () => CancelHandle;
}

let addon: NativeAddon | null = null;

export function loadNative(): NativeAddon {
  if (addon) return addon;
  const besideWorker = process.env.HYPER_DRV ? path.join(path.dirname(process.env.HYPER_DRV), ADDON) : null;
  const file = process.env.HYPER_ADDON ||
    (besideWorker && fs.existsSync(besideWorker) ? besideWorker : null) ||
    tryResolve('HYPER_ADDON', ADDON);
  if (!file) throw missing(ADDON, 'HYPER_ADDON');
  addon = requireFromHere(file) as NativeAddon;
  return addon;
}

function binaries(): { worker: string | null; qpdf: string | null } {
  return { worker: tryResolve('HYPER_DRV', 'hpdf-worker' + EXE), qpdf: tryResolve('HYPER_QPDF', 'qpdf' + EXE) };
}

function unwrap<T>(raw: string): T {
  const parsed = JSON.parse(raw) as { result?: T; error?: { code: HyperErrorCode; message: string } };
  if (parsed.error) throw new HyperError(parsed.error.code, parsed.error.message);
  return parsed.result as T;
}

export interface CompressInput {
  sourcePath: string;
  savePath?: string | undefined;
  password?: string | null | undefined;
  preset?: CompressLevel | undefined;
  options?: Partial<HyperCompressOptions> | undefined;
  targetSizeBytes?: number | undefined;
  report?: boolean | string | undefined;
  signal?: AbortSignal | undefined;
  timeoutMs?: number | undefined;
  totalTimeoutMs?: number | undefined;
}

export interface CompressResult {
  outputPath: string;
  originalSize: number;
  compressedSize: number;
  signed: boolean;
  signaturesRemoved: boolean;
  pdfa: PdfaLevel | null;
  pdfaPreserved: boolean;
  metTarget: boolean | null;
  warnings: string[];
  report?: HyperReport | undefined;
}

export function resolveOptions(input: CompressInput): HyperCompressOptions {
  const base = (input.preset && HYPER_PRESETS[input.preset]) || HYPER_PRESETS.medium;
  return normalizeHyperOptions({ ...base, ...(input.options ?? {}) });
}

export async function verifyPassword(sourcePath: string, password: string, timeoutMs?: number): Promise<boolean> {
  const native = loadNative();
  return unwrap<boolean>(await native.verifyPasswordNative({ sourcePath, password, timeoutMs, ...binaries() }));
}

export async function compress(input: CompressInput): Promise<CompressResult> {
  if (!input.sourcePath) throw new HyperError('engine_error', 'sourcePath is required');
  const native = loadNative();
  const bins = binaries();
  if (!bins.worker) throw missing('hpdf-worker', 'HYPER_DRV');
  const throwIfAborted = (): void => {
    if (input.signal?.aborted) {
      const reason: unknown = input.signal.reason;
      throw reason instanceof HyperError ? reason : new HyperError('cancelled');
    }
  };
  throwIfAborted();
  const handle = new native.CancelHandle();
  const onAbort = (): void => handle.cancel();
  input.signal?.addEventListener('abort', onAbort, { once: true });
  try {
    const raw = await native.compressNative(
      {
        sourcePath: input.sourcePath,
        savePath: input.savePath,
        password: input.password ?? null,
        preset: input.preset,
        options: input.options ?? {},
        targetSizeBytes: input.targetSizeBytes,
        report: input.report,
        timeoutMs: input.timeoutMs,
        totalTimeoutMs: input.totalTimeoutMs,
        ...bins,
      },
      handle,
    );
    const result = unwrap<CompressResult>(raw);
    if (result.report === undefined) delete result.report;
    return result;
  } catch (err) {
    if (err instanceof HyperError && err.code === 'cancelled') throwIfAborted();
    throw err;
  } finally {
    input.signal?.removeEventListener('abort', onAbort);
  }
}
