---
title: Encrypted PDFs
description: Compress password-protected PDFs from every surface, check a password before you compress, and know what the output looks like.
---

# Encrypted PDFs

Some PDFs are encrypted and need a password to open. In this guide you check a password, compress an encrypted PDF with Hyper Compress, and handle a wrong password.

A compressed output is written **without encryption**. Hyper decrypts the file first and compresses the decrypted copy, so anyone who receives the output can open it without a password.

## Before you begin

- Hyper installed for at least one surface. See the [Quickstart](/getting-started).
- An encrypted PDF, saved as `input.pdf`, and its password. This guide uses `hunter2`.
- Know which password you have. A PDF can have two:
  - the **user password** (open password), which a reader asks for before it shows the document;
  - the **owner password** (permissions password), which controls printing, copying and editing.

  Hyper accepts either one. Both unlock the file completely.

## 1. Check the password

When the password comes from a person, check it before you queue real work. This decrypts the file without compressing it.

::: code-group

```js [Node.js]
import { verifyPassword } from 'hyper-compress';

const ok = await verifyPassword('input.pdf', 'hunter2');
console.log(ok);
```

```bash [HTTP]
curl -s --data-binary @input.pdf -H 'X-Password-Encoded: hunter2' \
  http://localhost:8080/api/verify-password
```

```js [WebAssembly]
import { readFile } from 'node:fs/promises';
import { verifyPassword } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
console.log(await verifyPassword(input, 'hunter2'));
```

:::

```text
true
```

The HTTP API answers `{"valid":true}` or `{"valid":false}`. The CLI has no password check; run the compression and look for `decrypt_failed`.

`true` means a compression with the same password will be accepted. An **unencrypted PDF returns `true` for any password**, so `true` does not tell you the file was encrypted.

| Surface | Signature | Notes |
|---|---|---|
| Node.js | `verifyPassword(sourcePath, password, timeoutMs?)` | `timeoutMs` defaults to 60000. Throws `engine_error` when the file does not exist, and `timeout` when the check runs past `timeoutMs`. |
| HTTP | `POST /api/verify-password` with the PDF as the body and `X-Password-Encoded` or `X-Password` | Shares the queue, upload limit and API token with `/api/compress`. Has a 60 second limit. |
| WebAssembly | `verifyPassword(input, password)` | `input` is a `Uint8Array`. |

## 2. Compress with the password

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --password hunter2
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  password: 'hunter2',
});
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf -H 'X-Password-Encoded: hunter2' \
  'http://localhost:8080/api/compress'
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { password: 'hunter2' });
await writeFile('output.pdf', result.data);
console.log(`${result.originalSize} -> ${result.compressedSize} bytes`);
```

:::

From the CLI you see:

```text
output.pdf
  250200 -> 156384 bytes (37% smaller)
```

`output.pdf` is not encrypted. If you need it encrypted, encrypt it again with your own tool after Hyper. The [C API](/c-api) also takes a `password` argument.

Over HTTP, send the password in `X-Password-Encoded`, percent-encoded as UTF-8 the way JavaScript's `encodeURIComponent` does it. A plain ASCII password such as `hunter2` needs no encoding. The older `X-Password` header still works for plain ASCII passwords; when you send both, `X-Password-Encoded` wins.

::: warning
Hyper removes the encryption. Treat the output as you would the unlocked document: do not store it or send it anywhere the encrypted original could not go.
:::

## 3. Handle a wrong or missing password

A wrong password, or no password for a file that needs one, stops the run with the `decrypt_failed` error. No output file is written.

| Surface | Wrong password | No password for a file that needs one |
|---|---|---|
| CLI | `error: decrypt_failed`, exit code 1 | `error: the PDF is encrypted and needs a password`, exit code 1 |
| Node.js | throws `HyperError` with `code` `decrypt_failed` | the same, with the message `the PDF is encrypted and needs a password` |
| HTTP | `400` with `{"error":"decrypt_failed"}` | the same |
| WebAssembly | throws `HyperError` with `code` `decrypt_failed` and the message `wrong or missing password` | the same, with the message `the PDF is encrypted and needs a password` |

```js
import { compress, HyperError } from 'hyper-compress';

try {
  await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', password: 'wrong' });
} catch (e) {
  if (e instanceof HyperError && e.code === 'decrypt_failed') {
    console.log('Ask for the password again');
  } else {
    throw e;
  }
}
```

A decryption that runs past the stage time limit fails with `timeout`, not `decrypt_failed`. The full list of error codes is on [Errors](/errors).

## How it works

With a password, Hyper first decrypts the file to a temporary copy, then runs every step on that copy: the engine pass, the repack, any [size target](/guides/size-targets) and the PDF/A handling. A compressed result is built from the decrypted copy, which is why it is not encrypted.

Hyper never returns a file larger than the one you sent. When nothing it produced is smaller than your encrypted input, you get your **original encrypted bytes** back, unchanged.

A [signed](/guides/signed-documents) document that is also encrypted comes back as your original encrypted bytes, byte for byte, with `signed: true`, so its signature stays valid.

### Files with only an owner password

A file with only an owner password opens without asking, so it needs no password:

| The file has | Without a password, Hyper |
|---|---|
| A user password | fails with `decrypt_failed` (see step 3). |
| Only an owner password | compresses it, and the output **stays encrypted** with the same owner password. |
| No encryption | compresses it normally. |

Send the owner password to get a decrypted output instead. An empty password (`''`) counts as no password everywhere.

### Password text

- **Only the first line is used**, in every surface. Everything from the first line break on is dropped, so a password read from a file with a trailing newline still works.
- **Non-ASCII passwords**, such as `pässwört`, work in the CLI, the Node.js SDK and the WebAssembly package. Over HTTP, send them percent-encoded in `X-Password-Encoded`:

  ```bash
  curl -o output.pdf --data-binary @input.pdf \
    -H 'X-Password-Encoded: p%C3%A4ssw%C3%B6rt' \
    'http://localhost:8080/api/compress'
  ```

  `X-Password` takes plain ASCII only. HTTP headers carry ASCII, and the server closes the connection without a response when `X-Password` contains other characters. In `X-Password-Encoded` a literal `+` stays a `+`; it is not read as a space.

## Troubleshooting

**`decrypt_failed` with a password you believe is right.** Check it with `verifyPassword`. If that also returns `false`, you may have a different password than the file uses. Check for spaces, and check the case. Over HTTP, check that a non-ASCII password is percent-encoded in `X-Password-Encoded`.

**`the PDF is encrypted and needs a password`.** The file has a user password and none was sent. Send it with `--password`, `password` or `X-Password-Encoded`.

**The output is still encrypted.** Either the file had only an owner password and you sent none, or Hyper could not make the file smaller and returned your original. Send the owner password to get a decrypted output when it compresses.

**The HTTP request ends with an empty reply.** The password in `X-Password` contains non-ASCII characters. Send it percent-encoded in `X-Password-Encoded` instead.

## Next steps

- [Signed documents](/guides/signed-documents): why signed PDFs come back untouched.
- [Errors and cancellation](/guides/errors-and-cancellation): handle every error code.
- [Running in production](/guides/production): queues, limits and untrusted uploads.
- [HTTP API](/http-api): every header and status code.
