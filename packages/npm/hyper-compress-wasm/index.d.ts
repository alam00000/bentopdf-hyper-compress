import type { HyperWasmEngine } from './lib/wasm/sdk.js';

export declare const compress: HyperWasmEngine['compress'];
export declare const verifyPassword: HyperWasmEngine['verifyPassword'];

export {
  createEngine,
  HYPER_PRESETS,
  buildHyperTokens,
  normalizeHyperOptions,
  HyperError,
} from './lib/wasm/sdk.js';

export type {
  HyperWasmEngine,
  HyperWasmModule,
  HyperWasmOptions,
  HyperWasmResult,
  HyperCompressOptions,
  CompressLevel,
  PdfaLevel,
  HyperErrorCode,
} from './lib/wasm/sdk.js';
