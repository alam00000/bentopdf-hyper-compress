---
title: Document structure and cleanup
description: Decide what Hyper Compress keeps and removes besides page content, from accessibility tags and annotations to metadata, scripts and fast web view.
---

# Document structure and cleanup

A PDF holds more than its pages: accessibility tags, comments, form fields, bookmarks, attachments, scripts, metadata and data that only the program that made it reads. Hyper Compress keeps all of this by default, except two kinds of data no viewer needs. This guide lists every structure option, shows a privacy clean-up and a "keep everything" recipe, and shows how to confirm what changed.

## Before you begin

- Install Hyper and compress one file first. See the [Quickstart](/getting-started).
- Know which preset you use. Every preset removes embedded page thumbnails and application private data; `high` also removes article threads and web capture information. Everything else on this page is off unless you turn it on.
- To confirm results, use the CLI or the Node.js SDK: they are the surfaces that produce the [compression report](/report).

## What each preset removes

| Option | `low` | `medium` | `high` | `lossless` |
|---|---|---|---|---|
| `removeThumbnails` | on | on | on | on |
| `removeAppData` | on | on | on | on |
| `removeThreads` | | | on | |
| `removeSpiderInfo` | | | on | |
| every other option on this page | | | | |

## 1. Keep or remove accessibility tags

Tags (the structure tree) describe headings, paragraphs, lists, tables and reading order. Screen readers rely on them. Hyper keeps them unless you set `removeStructTree: true`.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeStructTree=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', options: { removeStructTree: true } });
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"removeStructTree":true}' \
  'http://localhost:8080/api/compress'
```

:::

Tags are usually a small part of a file. Remove them only when no one will read the document with assistive technology. When Hyper keeps a PDF/A file conformant at level A (`preserveConformance` on a PDF/A-1a, 2a or 3a file), the tags are always kept, because that level requires them.

## 2. Choose what happens to annotations, forms and links

`annotations` controls markup annotations: comments, highlights, stamps, drawings and sticky notes.

| `annotations` | Result |
|---|---|
| `keep` | Left as they are. The default. |
| `flatten` | Drawn into the page. They look the same but can no longer be edited or replied to. |
| `remove` | Deleted. |

Form fields and links are separate:

| Option | Result |
|---|---|
| `flattenForms: true` | Form fields are drawn into the page and stop being fillable. |
| `flattenLinks: true` | Links are drawn into the page and stop being clickable. Any visible link border stays. |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set annotations=flatten --set flattenForms=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { annotations: 'flatten', flattenForms: true },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"annotations":"flatten","flattenForms":true}' \
  'http://localhost:8080/api/compress'
```

:::

The older option `removeAnnots` is deprecated. `removeAnnots: true` means `annotations: 'flatten'`, which is what it always did, and only takes effect while `annotations` is `keep`. Use `annotations` in new code.

## 3. Remove interactive parts

| Option | Removes | What keeps working |
|---|---|---|
| `removeBookmarks` | The bookmarks (outline) panel. | Page content and links. |
| `removeAttachments` | Embedded files: document attachments and file attachment annotations. | Everything else. |
| `removeActions` | JavaScript and other actions that run code or open things: the open action, document, page and annotation triggers, document-level scripts, and JavaScript, launch, import, submit and media actions on links and bookmarks. | Plain links and bookmark jumps. |
| `removeNamedDests` | The named destinations table. | Links, bookmarks and the open action that point at a named destination are first rewritten to point at the page directly. Links from other documents to a name in this one stop working. |
| `removeThreads` | Article threads, the reading order chains some magazines and newspapers use to jump between columns. | Everything else. |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeActions=true --set removeAttachments=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { removeActions: true, removeAttachments: true },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"removeActions":true,"removeAttachments":true}' \
  'http://localhost:8080/api/compress'
```

:::

## 4. Remove metadata and leftover data

| Option | Removes |
|---|---|
| `removeMetadata` | The document information (title, author, producer, dates and custom entries) and the document XMP metadata packet. |
| `removePrintSettings` | Print presets: scaling, duplex, paper tray, page range and copies. Other viewer preferences stay. |
| `removeThumbnails` | Embedded page thumbnails. Viewers draw their own. |
| `removeAppData` | Application private data (PieceInfo) that programs like Illustrator store to reopen the file for editing. Viewers never use it. |
| `removeSpiderInfo` | Web capture information (SpiderInfo) left by tools that converted web pages to PDF. |
| `removeSearchIndex` | Web capture content identifiers (the IDS and URLS name trees). |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeMetadata=true --set removePrintSettings=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { removeMetadata: true, removePrintSettings: true },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"removeMetadata":true,"removePrintSettings":true}' \
  'http://localhost:8080/api/compress'
```

:::

PDF/A requires the XMP packet. When Hyper keeps a PDF/A file conformant (`preserveConformance`), `removeMetadata` is refused with the warning `removeMetadata refused: PDF/A requires the XMP metadata packet`.

## 5. Set the output format

| Option | Values | What it does |
|---|---|---|
| `minVersion` | `none`, `1.4`, `1.5`, `1.6`, `1.7`, `2.0` | Minimum PDF version in the file header. A lower version is raised; a higher one is never lowered. |
| `linearize` | `true` or `false` | Fast web view: a browser can show the first page before the whole file has downloaded. Adds a little size. |

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set linearize=true --set minVersion=1.7
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { linearize: true, minVersion: '1.7' },
});
console.log(result.warnings);
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"linearize":true,"minVersion":"1.7"}' \
  'http://localhost:8080/api/compress'
```

:::

`linearize` is skipped, with a reason, in these cases:

| Case | What you see |
|---|---|
| The input is digitally signed | The original is returned untouched and `signed` is `true`. |
| Hyper is keeping a PDF/A file conformant | `linearize skipped to preserve PDF/A conformance` |
| The WebAssembly SDK | `linearize is not available in the WebAssembly build` |
| The linearized file was not smaller than the input | `linearize not applied: the optimized file was not smaller than the original, so the original was kept` |

The C API has no linearize option. `minVersion: '2.0'` is refused while keeping a PDF/A-1, 2 or 3 file conformant, with the warning `minVersion 2.0 refused: this PDF/A level requires a 1.x header`.

## 6. Confirm what changed

Ask for a [compression report](/report). Its `cleanup` list counts each kind of structure before and after.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeMetadata=true --set removeActions=true --report report.json
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { removeMetadata: true, removeActions: true },
  report: true,
});
console.log(result.report.cleanup);
```

:::

```json
[
  { "item": "documentInfo", "before": 4, "after": 0 },
  { "item": "bookmarks", "before": 1, "after": 1 },
  { "item": "actions", "before": 1, "after": 0 }
]
```

Only items present in the input or the output are listed. The items are `documentInfo`, `xmpMetadata`, `bookmarks`, `attachments`, `actions`, `namedDestinations`, `printSettings`, `annotations`, `formFields`, `links`, `thumbnails`, `structureTree`, `articleThreads`, `outputIntents` and `searchIndex`; the [report reference](/report) says what each one counts. Application private data and web capture information have no item.

The HTTP API and the WebAssembly SDK do not produce a report. To check their output, run the same settings once through the CLI.

## Recipe: privacy clean-up

Remove what can identify people or run code before you share a document: metadata, scripts, attachments, comments and leftover web capture and application data.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf \
  --set removeMetadata=true --set removeActions=true \
  --set removeAttachments=true --set annotations=remove \
  --set removeSpiderInfo=true --set removeSearchIndex=true
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: {
    removeMetadata: true,
    removeActions: true,
    removeAttachments: true,
    annotations: 'remove',
    removeSpiderInfo: true,
    removeSearchIndex: true,
  },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"removeMetadata":true,"removeActions":true,"removeAttachments":true,"annotations":"remove","removeSpiderInfo":true,"removeSearchIndex":true}' \
  'http://localhost:8080/api/compress'
```

```bash [Docker]
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf \
  --set removeMetadata=true --set removeActions=true \
  --set removeAttachments=true --set annotations=remove \
  --set removeSpiderInfo=true --set removeSearchIndex=true
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, {
  options: {
    removeMetadata: true,
    removeActions: true,
    removeAttachments: true,
    annotations: 'remove',
    removeSpiderInfo: true,
    removeSearchIndex: true,
  },
});
await writeFile('output.pdf', result.data);
```

```rust [Rust]
use hyper_compress::{compress, Binaries, CompressInput, Preset};
use serde_json::json;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let options = json!({
        "removeMetadata": true,
        "removeActions": true,
        "removeAttachments": true,
        "annotations": "remove",
        "removeSpiderInfo": true,
        "removeSearchIndex": true
    });
    let result = compress(&CompressInput {
        source_path: "input.pdf".into(),
        save_path: Some("output.pdf".into()),
        preset: Some(Preset::Medium),
        options: options.as_object().cloned().unwrap_or_default(),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    })?;
    println!("{} -> {} bytes", result.original_size, result.compressed_size);
    Ok(())
}
```

```c [C]
/* the medium preset pairs with id 20 = 8926626:
   preset bits 0x2082 + metadata 0x100 + actions 0x1000 + attachments 0x20
   + remove annotations 0x800000 + web capture 0x80000 + search index 0x400 */
const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 6, 10, 13, 14, 16, 17, 20, 67};
const int vals[] = {150, 50, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1, 8926626, 6};
int rc = hpdf_compress_file("input.pdf", "output.pdf", ids, vals,
                            sizeof ids / sizeof ids[0], NULL);
```

:::

::: warning
This clean-up removes document-level data only. It does not redact anything drawn on the pages: names, signatures and text in the page content and in images stay visible.
:::

## Recipe: keep everything

Every preset removes page thumbnails and application private data. Turn both off to keep every structure in the document. With the `high` preset, also set `removeThreads` and `removeSpiderInfo` to `false`.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --set removeThumbnails=false --set removeAppData=false
```

```js [Node.js]
import { compress } from 'hyper-compress';

await compress({
  sourcePath: 'input.pdf',
  savePath: 'output.pdf',
  options: { removeThumbnails: false, removeAppData: false },
});
```

```bash [HTTP]
curl -o output.pdf --data-binary @input.pdf \
  --url-query 'options={"removeThumbnails":false,"removeAppData":false}' \
  'http://localhost:8080/api/compress'
```

```bash [Docker]
docker run --rm -v "$PWD":/work ghcr.io/alam00000/bentopdf-hyper-compress:latest \
  input.pdf output.pdf --set removeThumbnails=false --set removeAppData=false
```

```js [WebAssembly]
import { readFile, writeFile } from 'node:fs/promises';
import { compress } from 'hyper-compress-wasm';

const input = new Uint8Array(await readFile('input.pdf'));
const result = await compress(input, { options: { removeThumbnails: false, removeAppData: false } });
await writeFile('output.pdf', result.data);
```

```rust [Rust]
use hyper_compress::{compress, Binaries, CompressInput, Preset};
use serde_json::json;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let options = json!({ "removeThumbnails": false, "removeAppData": false });
    let result = compress(&CompressInput {
        source_path: "input.pdf".into(),
        save_path: Some("output.pdf".into()),
        preset: Some(Preset::Medium),
        options: options.as_object().cloned().unwrap_or_default(),
        binaries: Binaries::beside_exe(),
        ..Default::default()
    })?;
    println!("{} -> {} bytes", result.original_size, result.compressed_size);
    Ok(())
}
```

```c [C]
/* the medium preset pairs with id 20 = 0: nothing removed or flattened */
const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 6, 10, 13, 14, 16, 17, 20, 67};
const int vals[] = {150, 50, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1, 0, 6};
int rc = hpdf_compress_file("input.pdf", "output.pdf", ids, vals,
                            sizeof ids / sizeof ids[0], NULL);
```

:::

With these settings the `cleanup` list in the report shows the same `before` and `after` for every item:

```json
[
  { "item": "documentInfo", "before": 4, "after": 4 },
  { "item": "bookmarks", "before": 1, "after": 1 },
  { "item": "actions", "before": 1, "after": 1 }
]
```

Image and font settings still apply. For files where every pixel must also stay the same, use the `lossless` preset with the same two overrides.

## Troubleshooting

**A removal did nothing.** Check the spelling: option names are case sensitive and unknown names are ignored without an error. Then check `warnings`: under `preserveConformance`, Hyper refuses removals that would break PDF/A. Finally, if the output equals the input size, Hyper returned your original file because the result was not smaller.

**Links stopped working after `removeNamedDests`.** Links inside the document are rewritten and keep working. Links from other documents that point at a name in this one cannot be rewritten.

**A form can no longer be filled in.** `flattenForms` is on, or `rasterizePages` drew the form into a page image.

**Some boxes are still interactive after `annotations: 'remove'`.** Form fields and links are not markup annotations, so this option leaves them. Use `flattenForms` and `flattenLinks` for those.

## Next steps

- [Compression report](/report): every cleanup item and what it counts.
- [Signed documents](/guides/signed-documents): why signed files come back untouched.
- [PDF/A archives](/guides/pdfa): what Hyper refuses to remove from an archive file.
- [Options reference](/options): every option with its default.
