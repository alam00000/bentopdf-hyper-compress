#!/usr/bin/env node
import { compress } from '../../sdk/node/engine.js';
import type { CompressLevel } from '../../sdk/node/presets.js';

export interface Parsed {
  input?: string | undefined;
  output?: string | undefined;
  preset: CompressLevel;
  password: string | null;
  overrides: Record<string, number | boolean>;
  targetSizeBytes?: number | undefined;
}

const PRESETS: readonly CompressLevel[] = ['low', 'medium', 'high', 'lossless'];

function usage(): never {
  process.stderr.write(
    'usage: hyper <input.pdf> <output.pdf> [--preset low|medium|high|lossless]\n' +
      '                                      [--password PW] [--set key=value ...]\n' +
      '                                      [--brotli] [--target-size N[KB|MB]]\n',
  );
  process.exit(2);
}

export function parse(argv: string[]): Parsed {
  const p: Parsed = { preset: 'medium', password: null, overrides: {} };
  const positional: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === undefined) continue;
    if (a === '--preset') {
      const v = argv[++i] as CompressLevel;
      if (!PRESETS.includes(v)) usage();
      p.preset = v;
    } else if (a === '--password') {
      p.password = argv[++i] ?? null;
    } else if (a === '--brotli') {
      p.overrides.brotli = true;
    } else if (a === '--target-size') {
      const raw = (argv[++i] ?? '').trim();
      const m = /^([0-9]+(?:\.[0-9]+)?)\s*(kb|mb|k|m|b)?$/i.exec(raw);
      if (!m || m[1] === undefined) usage();
      const unit = (m[2] ?? 'b').toLowerCase();
      const mult = unit.startsWith('k') ? 1024 : unit.startsWith('m') ? 1024 * 1024 : 1;
      p.targetSizeBytes = Math.floor(Number(m[1]) * mult);
      if (!(p.targetSizeBytes > 0)) usage();
    } else if (a === '--set') {
      const kv = argv[++i] ?? '';
      const eq = kv.indexOf('=');
      if (eq <= 0) usage();
      const key = kv.slice(0, eq);
      const raw = kv.slice(eq + 1);
      p.overrides[key] =
        raw === 'true' ? true : raw === 'false' ? false : Number(raw);
    } else if (a.startsWith('--')) {
      usage();
    } else {
      positional.push(a);
    }
  }
  p.input = positional[0];
  p.output = positional[1];
  return p;
}

async function main(): Promise<void> {
  const p = parse(process.argv.slice(2));
  if (!p.input || !p.output) usage();
  const res = await compress({
    sourcePath: p.input,
    savePath: p.output,
    preset: p.preset,
    password: p.password,
    options: p.overrides as never,
    targetSizeBytes: p.targetSizeBytes,
  });
  for (const w of res.warnings) {
    process.stderr.write(`warning: ${w}\n`);
  }
  const pct =
    res.originalSize > 0
      ? Math.round((1 - res.compressedSize / res.originalSize) * 100)
      : 0;
  process.stdout.write(
    `${res.outputPath}\n` +
      `  ${res.originalSize} -> ${res.compressedSize} bytes (${pct}% smaller)` +
      `${res.signed ? ' [signed: original preserved]' : ''}` +
      `${res.metTarget === null ? '' : res.metTarget ? ' [target met]' : ' [target NOT met  - best effort]'}\n`,
  );
}

import { pathToFileURL } from 'node:url';
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((err: unknown) => {
    process.stderr.write(`error: ${err instanceof Error ? err.message : String(err)}\n`);
    process.exit(1);
  });
}
