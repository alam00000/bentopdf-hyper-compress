#!/usr/bin/env node
import { realpathSync } from 'node:fs';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { compress } from '../../sdk/node/engine.js';
import type { CompressLevel } from '../../sdk/node/presets.js';

export interface Parsed {
  input?: string | undefined;
  output?: string | undefined;
  preset: CompressLevel;
  password: string | null;
  overrides: Record<string, number | boolean>;
  targetSizeBytes?: number | undefined;
  help: boolean;
  version: boolean;
}

const PRESETS: readonly CompressLevel[] = ['low', 'medium', 'high', 'lossless'];

const USAGE =
  'usage: hyper <input.pdf> [output.pdf] [--preset low|medium|high|lossless]\n' +
  '                                      [--password PW] [--set key=value ...]\n' +
  '                                      [--brotli] [--target-size N[KB|MB]]\n';

const HELP = `hyper - high fidelity, content preserving PDF compression

${USAGE}
options:
  --preset <name>       low, medium (default), high or lossless
  --password <pw>       password for an encrypted input
  --target-size <size>  aim for a size, e.g. 2MB, 500KB or a byte count
  --brotli              PDF 2.0 Brotli streams; the reader must support them
  --set <key=value>     override any engine option; repeatable
  -h, --help            show this help
  -v, --version         show the version

Leave out the output path and the result is written next to the input as
<input>-compressed.pdf.

examples:
  hyper report.pdf
  hyper in.pdf out.pdf
  hyper in.pdf out.pdf --preset high
  hyper scan.pdf out.pdf --target-size 2MB
  hyper locked.pdf out.pdf --password hunter2
  hyper in.pdf out.pdf --set grayscale=true --set imageQuality=40

The output is never larger than the input, signed documents are returned
untouched, and anything skipped for safety is reported as a warning.

exit codes: 0 success, 1 error, 2 bad usage
options reference: https://hyper.bentopdf.com/docs/options
`;

export function version(): string {
  try {
    const req = createRequire(import.meta.url);
    const pkg = req('../../../package.json') as { version?: string };
    return pkg.version ?? 'unknown';
  } catch {
    return 'unknown';
  }
}

function usage(message?: string): never {
  if (message) process.stderr.write(`error: ${message}\n`);
  process.stderr.write(USAGE);
  process.stderr.write('run "hyper --help" for the full option list\n');
  process.exit(2);
}

export function parse(argv: string[]): Parsed {
  const p: Parsed = {
    preset: 'medium',
    password: null,
    overrides: {},
    help: false,
    version: false,
  };
  const positional: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === undefined) continue;
    if (a === '--help' || a === '-h') {
      p.help = true;
    } else if (a === '--version' || a === '-v' || a === '-V') {
      p.version = true;
    } else if (a === '--preset') {
      const v = argv[++i] as CompressLevel | undefined;
      if (v === undefined || !PRESETS.includes(v)) {
        usage(`--preset expects one of ${PRESETS.join(', ')}`);
      }
      p.preset = v;
    } else if (a === '--password') {
      p.password = argv[++i] ?? null;
    } else if (a === '--brotli') {
      p.overrides.brotli = true;
    } else if (a === '--target-size') {
      const raw = (argv[++i] ?? '').trim();
      const m = /^([0-9]+(?:\.[0-9]+)?)\s*(kb|mb|k|m|b)?$/i.exec(raw);
      if (!m || m[1] === undefined) {
        usage('--target-size expects a size like 2MB, 500KB or a byte count');
      }
      const unit = (m[2] ?? 'b').toLowerCase();
      const mult = unit.startsWith('k') ? 1024 : unit.startsWith('m') ? 1024 * 1024 : 1;
      p.targetSizeBytes = Math.floor(Number(m[1]) * mult);
      if (!(p.targetSizeBytes > 0)) usage('--target-size must be greater than zero');
    } else if (a === '--set') {
      const kv = argv[++i] ?? '';
      const eq = kv.indexOf('=');
      if (eq <= 0) usage('--set expects key=value');
      const key = kv.slice(0, eq);
      const raw = kv.slice(eq + 1);
      p.overrides[key] =
        raw === 'true' ? true : raw === 'false' ? false : Number(raw);
    } else if (a.startsWith('-')) {
      usage(`unknown flag ${a}`);
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
  if (p.help) {
    process.stdout.write(HELP);
    return;
  }
  if (p.version) {
    process.stdout.write(`${version()}\n`);
    return;
  }
  if (!p.input) usage('an input PDF is required');
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

const invoked = process.argv[1];
if (invoked) {
  let real = invoked;
  try {
    real = realpathSync(invoked);
  } catch {}
  if (import.meta.url === pathToFileURL(real).href) {
    main().catch((err: unknown) => {
      process.stderr.write(`error: ${err instanceof Error ? err.message : String(err)}\n`);
      process.exit(1);
    });
  }
}
