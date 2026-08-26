import type { HyperWasmModule, HyperWasmOptions, HyperWasmResult } from './glue.js';
import { compressBuffer, decryptBuffer } from './glue.js';
import { HyperError } from '../sdk/node/errors.js';

export interface HyperWasmEngine {
  init(): Promise<void>;
  compress(input: Uint8Array, opts?: HyperWasmOptions): Promise<HyperWasmResult>;
  verifyPassword(input: Uint8Array, password: string): Promise<boolean>;
}

function describe(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

export function createEngine(load: () => Promise<HyperWasmModule>): HyperWasmEngine {
  let pending: Promise<HyperWasmModule> | null = null;
  const getModule = (): Promise<HyperWasmModule> => {
    pending ??= load().catch((err: unknown) => {
      pending = null;
      throw new HyperError('engine_error', `wasm module failed to load: ${describe(err)}`);
    });
    return pending;
  };
  return {
    async init(): Promise<void> {
      await getModule();
    },
    async compress(input: Uint8Array, opts: HyperWasmOptions = {}): Promise<HyperWasmResult> {
      const mod = await getModule();
      try {
        return compressBuffer(mod, input, opts);
      } catch (err) {
        if (err instanceof HyperError) throw err;
        throw new HyperError('engine_error', describe(err));
      }
    },
    async verifyPassword(input: Uint8Array, password: string): Promise<boolean> {
      const mod = await getModule();
      return decryptBuffer(mod, input, password) !== null;
    },
  };
}

export { HYPER_PRESETS } from '../sdk/node/presets.js';
export type { CompressLevel } from '../sdk/node/presets.js';
export { buildHyperTokens, normalizeHyperOptions } from '../sdk/node/options.js';
export type { HyperCompressOptions } from '../sdk/node/options.js';
export type { PdfaLevel } from '../sdk/node/pdfa.js';
export { HyperError } from '../sdk/node/errors.js';
export type { HyperErrorCode } from '../sdk/node/errors.js';
export type { HyperWasmModule, HyperWasmOptions, HyperWasmResult } from './glue.js';
