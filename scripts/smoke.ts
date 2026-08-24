import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { compress } from '../sdk/node/index.js';
import type { CompressLevel } from '../sdk/node/index.js';

const PRESETS: readonly CompressLevel[] = ['low', 'medium', 'high', 'lossless'];

async function main(): Promise<void> {
  const input = process.argv[2];
  if (!input || !fs.existsSync(input)) {
    process.stderr.write('usage: smoke <input.pdf>\n');
    process.exit(2);
  }
  const tmp = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'hyper-smoke-'));
  let failures = 0;
  for (const preset of PRESETS) {
    const out = path.join(tmp, `${preset}.pdf`);
    const res = await compress({ sourcePath: input, savePath: out, preset });
    const exists = fs.existsSync(out) && res.compressedSize > 0;
    const neverBigger = res.compressedSize <= res.originalSize;
    const ok = exists && neverBigger;
    if (!ok) failures++;
    const pct =
      res.originalSize > 0
        ? Math.round((1 - res.compressedSize / res.originalSize) * 100)
        : 0;
    process.stdout.write(
      `${ok ? 'PASS' : 'FAIL'}  ${preset.padEnd(9)} ` +
        `${res.originalSize} -> ${res.compressedSize} (${pct}%)` +
        `${res.signed ? ' [signed]' : ''}\n`,
    );
  }
  await fs.promises.rm(tmp, { recursive: true, force: true });
  process.stdout.write(failures === 0 ? '\nall presets OK\n' : `\n${failures} failed\n`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((err: unknown) => {
  process.stderr.write(`error: ${err instanceof Error ? err.message : String(err)}\n`);
  process.exit(1);
});
