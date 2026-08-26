import createModule from './engine/hyper-compress.js';
import { createEngine } from './lib/wasm/sdk.js';

const engine = createEngine(() => createModule());

export const { compress, verifyPassword } = engine;

export {
  createEngine,
  HYPER_PRESETS,
  buildHyperTokens,
  normalizeHyperOptions,
  HyperError,
} from './lib/wasm/sdk.js';
