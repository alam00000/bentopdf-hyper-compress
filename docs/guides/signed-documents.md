---
title: Signed documents
description: Understand why Hyper returns digitally signed PDFs untouched, detect it in every surface, and remove signatures when a smaller file matters more.
---

# Signed documents

A digitally signed PDF carries a cryptographic seal over its exact bytes. In this guide you see what Hyper Compress does with a signed PDF, how to detect it in your code, and how to compress one anyway when you no longer need the signature.

By default a signed PDF comes back **byte for byte unchanged**, so its signature stays valid.

## Before you begin

- Hyper installed for at least one surface. See the [Quickstart](/getting-started).
- A digitally signed PDF, saved as `input.pdf`.

## 1. Compress a signed PDF

Run a normal compression. There is nothing to switch on.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf' });
console.log(result.signed, result.originalSize, result.compressedSize);
```

```bash [HTTP]
curl -s -o output.pdf -D - --data-binary @input.pdf \
  'http://localhost:8080/api/compress' | grep -i '^x-signed'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input);
await writeFile('output.pdf', result.data);
console.log(result.signed, result.originalSize, result.compressedSize);
```

:::

From the CLI you see:

```text
output.pdf
  14636 -> 14636 bytes (0% smaller) [signed: original preserved]
```

`output.pdf` is an exact copy of `input.pdf`. Each surface tells you why nothing shrank:

| Surface | Signal |
|---|---|
| CLI | `[signed: original preserved]` after the sizes |
| Node.js, WebAssembly | `signed: true` |
| HTTP | `X-Signed: true` |
| C API | return value (or `*out_status`) `2`: the input is signed and the output is an unchanged copy. See [C API](/c-api). |

This is an outcome, not an error. The CLI exits with code 0 and the HTTP API answers 200.

## 2. Branch on the result

Use the flag to tell a signed document apart from one that could not be compressed:

```js
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf' });

if (result.signed) {
  console.log('Signed: returned unchanged so the signature stays valid');
} else if (result.compressedSize === result.originalSize) {
  console.log('Already as small as these settings allow');
}
```

## 3. Remove the signatures to compress

If a smaller file matters more than the signature, set `removeSignatures`. Hyper removes the digital signatures, keeps the visible signature on the page, and compresses the document.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeSignatures=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { removeSignatures: true },
});
console.log(result.signaturesRemoved, result.warnings);
```

```bash [HTTP]
curl -s -o output.pdf -D - --data-binary @input.pdf \
  --url-query 'options={"removeSignatures":true}' \
  'http://localhost:8080/api/compress' | grep -i '^x-'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { options: { removeSignatures: true } });
await writeFile('output.pdf', result.data);
console.log(result.signaturesRemoved, result.warnings);
```

:::

From the CLI you see:

```text
warning: digital signatures removed so the document could be compressed
output.pdf
  14636 -> 1721 bytes (88% smaller)
```

::: warning
The output is no longer signed. Nothing proves any more who signed it or that it is unchanged. Keep the signed original if you need that proof. The output can be signed again.
:::

How each surface reports the removal:

| Surface | Signal |
|---|---|
| CLI | the `warning:` line above |
| Node.js, WebAssembly | `signaturesRemoved: true`, `signed: false`, and the warning in `warnings` |
| HTTP | `X-Signatures-Removed: true`, and the warning in `X-Warnings` |
| C API | return value `0` |

`signaturesRemoved` is `true` only when the compressed file was used. If removing the signatures and compressing did not make the file smaller, Hyper returns your original bytes, which are still signed, with `signed: true`, `signaturesRemoved: false` and no warning.

## How it works

Hyper treats a document as signed when it has a completed signature: a signature form field that holds a signature value, or document-level signature permissions (the catalog's `Perms` entry). An empty signature field, waiting for someone to sign, does not count.

A signature covers the exact bytes of the file. Rewriting anything, even without a visible change, moves bytes and breaks it. So for a signed document Hyper skips every step and copies the input to the output:

- Every option is ignored, including `linearize`, with no warning.
- A [size target](/guides/size-targets) is not searched. `metTarget` says whether the original fits, and if it does not, you get the warning `target of <n> bytes not reached: signed documents are returned untouched; set removeSignatures to compress them`.
- A [PDF/A](/guides/pdfa) claim is untouched, so `pdfaPreserved` is `true` in Node.js and `pdfaOutcome` is `preserved` in WebAssembly.
- A [report](/report) is still built when you ask for one.

`removeSignatures` removes the value from every signature field (with its seed value and lock settings), the document's signature permissions and the form's signature flags. The signature fields themselves stay, now empty, which is why the document can be signed again. The engine then no longer sees a signed document and compresses it like any other.

### Empty signature fields

`removeSignatureAppearances` removes the visible appearance of **empty (unsigned)** signature fields only. It never touches a completed signature. On a signed document it has no effect unless you also set `removeSignatures`, because a signed document is returned untouched.

### Signed and encrypted

A signed document that is also [encrypted](/guides/encrypted-pdfs) and opened with a password comes back as your original encrypted bytes, byte for byte, with `signed: true`. The signature stays valid, and the output is still encrypted.

## Troubleshooting

**A file comes back unchanged but is not reported as signed.** Hyper could not make it smaller with these settings. See [Choose the right settings](/guides/choosing-settings).

**`removeSignatures` was set but the output is still signed.** The compressed file was not smaller, so Hyper kept the original. `signed` is `true` and `signaturesRemoved` is `false` in that case.

**Your app needs to know about removed signatures over HTTP.** Read `X-Signatures-Removed`, which is `true` only when the signatures were removed and the compressed file was returned.

## Next steps

- [Guarantees](/guarantees): what Hyper promises never to do to your files.
- [Encrypted PDFs](/guides/encrypted-pdfs): passwords and decrypted output.
- [Document structure](/guides/document-structure): forms, annotations and other cleanups.
- [Warnings](/warnings): every warning Hyper can return.
