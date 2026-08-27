import { buildHyperTokens, normalizeHyperOptions, type HyperCompressOptions } from '../sdk/node/options.js';
import { HYPER_PRESETS, type CompressLevel } from '../sdk/node/presets.js';
import { HyperError } from '../sdk/node/errors.js';
import {
  TARGET_QUALITY_FLOOR, TARGET_QUALITY_STEPS, targetLadder, targetMissWarning,
  targetStartQuality,
} from '../sdk/node/target.js';
import {
  conformanceSafeOptions, formatPdfaLevel, packAllowedWhilePreserving, parsePdfaFromXmp,
  restoreHeaderVersion, stripPdfaFromXmp, type PdfaLevel,
} from '../sdk/node/pdfa.js';

export interface HyperWasmModule {
  _hyper_compress_buffer(
    inPtr: number,
    inSize: number,
    passwordPtr: number,
    idsPtr: number,
    valsPtr: number,
    optCount: number,
    outSizePtr: number,
    outSignedPtr: number,
  ): number;
  _hyper_decrypt_buffer(
    inPtr: number,
    inSize: number,
    passwordPtr: number,
    outSizePtr: number,
  ): number;
  _hyper_pack_buffer(inPtr: number, inSize: number, outSizePtr: number): number;
  _hyper_pack_buffer_pdfa(inPtr: number, inSize: number, outSizePtr: number): number;
  _hyper_get_xmp(
    inPtr: number, inSize: number, passwordPtr: number, outSizePtr: number,
  ): number;
  _hyper_set_xmp(
    inPtr: number, inSize: number, xmpPtr: number, xmpSize: number,
    outSizePtr: number,
  ): number;
  _hyper_free(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAP32: Int32Array;
}

export interface HyperWasmOptions {
  preset?: CompressLevel | undefined;
  options?: Partial<HyperCompressOptions> | undefined;
  password?: string | null | undefined;
  targetSizeBytes?: number | undefined;
}

export interface HyperWasmResult {
  data: Uint8Array;
  originalSize: number;
  compressedSize: number;
  signed: boolean;
  pdfa: PdfaLevel | null;
  pdfaOutcome: 'none' | 'preserved' | 'claim-withdrawn';
  metTarget: boolean | null;
  warnings: string[];
}

function tokensToPairs(tokens: string[]): { ids: number[]; vals: number[] } {
  const ids: number[] = [];
  const vals: number[] = [];
  for (const t of tokens) {
    const eq = t.indexOf('=');
    if (eq <= 0) continue;
    const id = Number(t.slice(0, eq));
    const val = Number(t.slice(eq + 1));
    if (Number.isFinite(id) && Number.isFinite(val)) {
      ids.push(id);
      vals.push(val);
    }
  }
  return { ids, vals };
}

function copyIn(mod: HyperWasmModule, bytes: Uint8Array): number {
  const ptr = mod._malloc(Math.max(1, bytes.length));
  if (ptr) mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

function copyCString(mod: HyperWasmModule, value: string | null): number {
  if (!value) return 0;
  const bytes = new TextEncoder().encode(value);
  const ptr = mod._malloc(bytes.length + 1);
  if (!ptr) return 0;
  mod.HEAPU8.set(bytes, ptr);
  mod.HEAPU8[ptr + bytes.length] = 0;
  return ptr;
}

function copyIntArray(mod: HyperWasmModule, values: number[]): number {
  const ptr = mod._malloc(Math.max(1, values.length) * 4);
  if (!ptr) return 0;
  const view = mod.HEAP32;
  for (let i = 0; i < values.length; i++) view[(ptr >> 2) + i] = values[i] ?? 0;
  return ptr;
}

function readHeapU32(mod: HyperWasmModule, byteOffset: number): number {
  const value = mod.HEAPU32[byteOffset >> 2];
  if (value === undefined) throw new HyperError('engine_error', 'wasm heap read out of bounds');
  return value;
}

function readHeap32(mod: HyperWasmModule, byteOffset: number): number {
  const value = mod.HEAP32[byteOffset >> 2];
  if (value === undefined) throw new HyperError('engine_error', 'wasm heap read out of bounds');
  return value;
}

function takeResult(mod: HyperWasmModule, outPtr: number, sizePtr: number): Uint8Array | null {
  const size = readHeapU32(mod, sizePtr) >>> 0;
  if (!outPtr || size === 0) {
    if (outPtr) mod._hyper_free(outPtr);
    return null;
  }
  const copy = mod.HEAPU8.slice(outPtr, outPtr + size);
  mod._hyper_free(outPtr);
  return copy;
}

export function decryptBuffer(
  mod: HyperWasmModule,
  input: Uint8Array,
  password: string,
): Uint8Array | null {
  const inPtr = copyIn(mod, input);
  const pwPtr = copyCString(mod, password);
  const sizePtr = mod._malloc(4);
  try {
    if (!inPtr || !sizePtr) return null;
    const outPtr = mod._hyper_decrypt_buffer(inPtr, input.length, pwPtr, sizePtr);
    return takeResult(mod, outPtr, sizePtr);
  } finally {
    mod._free(inPtr);
    if (pwPtr) mod._free(pwPtr);
    mod._free(sizePtr);
  }
}

export function packBuffer(
  mod: HyperWasmModule,
  input: Uint8Array,
  pdfaSafe = false,
): Uint8Array | null {
  const inPtr = copyIn(mod, input);
  const sizePtr = mod._malloc(4);
  try {
    if (!inPtr || !sizePtr) return null;
    const outPtr = pdfaSafe
      ? mod._hyper_pack_buffer_pdfa(inPtr, input.length, sizePtr)
      : mod._hyper_pack_buffer(inPtr, input.length, sizePtr);
    return takeResult(mod, outPtr, sizePtr);
  } finally {
    mod._free(inPtr);
    mod._free(sizePtr);
  }
}

export function getXmp(
  mod: HyperWasmModule,
  input: Uint8Array,
  password: string | null = null,
): string | null {
  const inPtr = copyIn(mod, input);
  const pwPtr = copyCString(mod, password);
  const sizePtr = mod._malloc(4);
  try {
    if (!inPtr || !sizePtr) return null;
    const outPtr = mod._hyper_get_xmp(inPtr, input.length, pwPtr, sizePtr);
    const bytes = takeResult(mod, outPtr, sizePtr);
    return bytes ? new TextDecoder().decode(bytes) : null;
  } finally {
    mod._free(inPtr);
    if (pwPtr) mod._free(pwPtr);
    mod._free(sizePtr);
  }
}

export function setXmp(
  mod: HyperWasmModule,
  input: Uint8Array,
  xmp: string,
): Uint8Array | null {
  const bytes = new TextEncoder().encode(xmp);
  const inPtr = copyIn(mod, input);
  const xmpPtr = copyIn(mod, bytes);
  const sizePtr = mod._malloc(4);
  try {
    if (!inPtr || !xmpPtr || !sizePtr) return null;
    const outPtr = mod._hyper_set_xmp(
      inPtr, input.length, xmpPtr, bytes.length, sizePtr);
    return takeResult(mod, outPtr, sizePtr);
  } finally {
    mod._free(inPtr);
    mod._free(xmpPtr);
    mod._free(sizePtr);
  }
}

export function detectPdfa(
  mod: HyperWasmModule,
  input: Uint8Array,
  password: string | null = null,
): PdfaLevel | null {
  const xmp = getXmp(mod, input, password);
  return xmp ? parsePdfaFromXmp(xmp) : null;
}

export function runEngine(
  mod: HyperWasmModule,
  input: Uint8Array,
  tokens: string[],
  password: string | null,
): { data: Uint8Array | null; signed: boolean } {
  const { ids, vals } = tokensToPairs(tokens);
  const inPtr = copyIn(mod, input);
  const pwPtr = copyCString(mod, password);
  const idsPtr = copyIntArray(mod, ids);
  const valsPtr = copyIntArray(mod, vals);
  const sizePtr = mod._malloc(4);
  const signedPtr = mod._malloc(4);
  try {
    if (!inPtr || !idsPtr || !valsPtr || !sizePtr || !signedPtr) {
      return { data: null, signed: false };
    }
    const outPtr = mod._hyper_compress_buffer(
      inPtr,
      input.length,
      pwPtr,
      idsPtr,
      valsPtr,
      ids.length,
      sizePtr,
      signedPtr,
    );
    const signed = readHeap32(mod, signedPtr) !== 0;
    return { data: takeResult(mod, outPtr, sizePtr), signed };
  } finally {
    mod._free(inPtr);
    if (pwPtr) mod._free(pwPtr);
    mod._free(idsPtr);
    mod._free(valsPtr);
    mod._free(sizePtr);
    mod._free(signedPtr);
  }
}

export function resolveOptions(opts: HyperWasmOptions): HyperCompressOptions {
  const base = opts.preset ? HYPER_PRESETS[opts.preset] : HYPER_PRESETS.medium;
  return normalizeHyperOptions({ ...base, ...(opts.options ?? {}) });
}

export function compressBuffer(
  mod: HyperWasmModule,
  input: Uint8Array,
  opts: HyperWasmOptions = {},
): HyperWasmResult {
  const originalSize = input.length;
  const password =
    typeof opts.password === 'string' && opts.password.length > 0 ? opts.password : null;

  let workIn = input;
  let fallbackBase = input;
  if (password) {
    const decrypted = decryptBuffer(mod, input, password);
    if (!decrypted) throw new HyperError('decrypt_failed', 'wrong or missing password');
    workIn = decrypted;
    fallbackBase = decrypted;
  }
  const baselineSize = fallbackBase.length;
  const target = opts.targetSizeBytes && opts.targetSizeBytes > 0
    ? Math.floor(opts.targetSizeBytes)
    : null;

  const requested = resolveOptions(opts);
  let resolved = requested;
  const warnings: string[] = [];

  const claimed = detectPdfa(mod, workIn);
  const preserving = Boolean(resolved.preserveConformance && claimed);
  if (preserving && claimed) {
    resolved = conformanceSafeOptions(resolved, claimed);
    if (requested.rasterizePages && !resolved.rasterizePages) {
      warnings.push('rasterizePages disabled to preserve PDF/A conformance');
    }
    if (requested.brotli) {
      warnings.push('brotli disabled to preserve PDF/A conformance');
    }
  }
  resolved = { ...resolved, preserveConformance: preserving };
  const tokens = buildHyperTokens(resolved);

  const enginePassword = password && workIn !== input ? null : password;
  const { data: engineOut, signed } = runEngine(mod, workIn, tokens, enginePassword);

  const finish = (data: Uint8Array): HyperWasmResult => {
    let out = data;
    let outcome: HyperWasmResult['pdfaOutcome'] = 'none';
    if (claimed) {
      if (preserving) {
        out = restoreHeaderVersion(workIn, out);
        outcome = 'preserved';
      } else if (out !== fallbackBase && out !== input) {
        const xmp = getXmp(mod, out);
        const stripped = xmp ? setXmp(mod, out, stripPdfaFromXmp(xmp)) : null;
        if (stripped) {
          out = stripped;
          outcome = 'claim-withdrawn';
        }
      } else {
        outcome = 'preserved';
      }
    }
    const metTarget = target != null ? out.length <= target : null;
    if (metTarget === false && target != null) {
      warnings.push(targetMissWarning(out.length, target));
    }
    return {
      data: out,
      originalSize,
      compressedSize: out.length,
      signed,
      pdfa: claimed,
      pdfaOutcome: outcome,
      metTarget,
      warnings,
    };
  };

  if (engineOut && signed) {
    return {
      data: fallbackBase,
      originalSize,
      compressedSize: fallbackBase.length,
      signed: true,
      pdfa: claimed,
      pdfaOutcome: claimed ? 'preserved' : 'none',
      metTarget: target != null ? fallbackBase.length <= target : null,
      warnings,
    };
  }

  const skipPack = preserving && !packAllowedWhilePreserving();

  let packed: Uint8Array | null = null;
  if (engineOut) {
    packed = skipPack ? engineOut : (packBuffer(mod, engineOut) ?? engineOut);
  } else if (!skipPack) {
    packed = packBuffer(mod, workIn);
  }

  if (target != null && packed && !preserving && packed.length > target) {
    const baseOpts: HyperCompressOptions = { ...resolved, lossless: false };
    const startQ = targetStartQuality(baseOpts);
    let bestFit: Uint8Array | null = null;
    let smallest = packed;
    let lo = TARGET_QUALITY_FLOOR;
    let hi = startQ - 1;
    let iterations = 0;
    const attempt = (o: HyperCompressOptions): Uint8Array | null => {
      const r = runEngine(mod, workIn, buildHyperTokens(o), enginePassword);
      if (!r.data || r.signed) return null;
      const p = skipPack ? r.data : (packBuffer(mod, r.data) ?? r.data);
      return p && p.length > 0 ? p : null;
    };
    while (lo <= hi && iterations < TARGET_QUALITY_STEPS) {
      iterations++;
      const q = Math.floor((lo + hi) / 2);
      const out = attempt({ ...baseOpts, imageQuality: q });
      if (!out) break;
      if (out.length < smallest.length) smallest = out;
      if (out.length <= target) {
        bestFit = out;
        lo = q + 1;
      } else {
        hi = q - 1;
      }
    }
    for (const rung of targetLadder(baseOpts)) {
      if (bestFit) break;
      const out = attempt(rung);
      if (!out) break;
      if (out.length <= target) bestFit = out;
      else if (out.length < smallest.length) smallest = out;
    }
    packed = bestFit ?? smallest;
  }

  const data =
    packed && packed.length > 0 && packed.length < baselineSize ? packed : fallbackBase;
  return finish(data);
}

export function describePdfaOutcome(r: HyperWasmResult): string {
  if (!r.pdfa) return '';
  const level = formatPdfaLevel(r.pdfa);
  return r.pdfaOutcome === 'preserved'
    ? `${level} preserved`
    : `${level} claim withdrawn`;
}
