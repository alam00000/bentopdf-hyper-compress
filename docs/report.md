---
title: Compression report
description: The JSON report of every font, page image and cleanup in one compression, field by field.
---

# Compression report

The compression report shows what Hyper Compress did inside one compression: each font, each page image and each piece of document structure, before and after. Use it to explain a result, to audit what a preset removes, or to find the images that make a file large.

::: code-group

```bash [CLI]
hyper input.pdf output.pdf --report report.json
```

```js [Node.js]
import { compress } from 'hyper-compress';

const result = await compress({ sourcePath: 'input.pdf', savePath: 'output.pdf', report: true });
for (const img of result.report?.images ?? []) {
  console.log(img.page, img.name, img.before.bytes, '->', img.after?.bytes);
}
```

```rust [Rust]
use std::path::PathBuf;

use hyper_compress::{compress, Binaries, CompressInput, ReportRequest};

fn main() {
    let input = CompressInput {
        source_path: PathBuf::from("input.pdf"),
        save_path: Some(PathBuf::from("output.pdf")),
        report: ReportRequest::Return,
        binaries: Binaries::beside_exe(),
        ..Default::default()
    };
    let result = compress(&input).expect("compress failed");
    if let Some(report) = result.report {
        for img in report.images {
            println!("{} {} {}", img.page, img.name, img.before.bytes);
        }
    }
}
```

:::

## Requesting a report

| Surface | How | Where the report goes |
|---|---|---|
| CLI | `--report <file>` | Written to the file as indented JSON. |
| Node.js SDK | `report: true` | `result.report` |
| Node.js SDK | `report: 'report.json'` | `result.report`, and written to the file as JSON. |
| Rust crate | `report: ReportRequest::Return` | `result.report` |
| Rust crate | `report: ReportRequest::File(path)` | `result.report`, and written to the file as JSON. |
| HTTP API, WebAssembly, C API | Not available. | |

A report adds two read passes with `qpdf`, one over the input and one over the output, so leave it off when you do not need it.

If the PDF structure cannot be read, or `qpdf` is not available, the compression still succeeds, no report is returned or written, and the warning `report unavailable: the PDF structure could not be read` is added. When the input is signed and returned unchanged, the report still describes it, with the same content before and after.

## Example

```json
{
  "originalSize": 601042,
  "compressedSize": 77105,
  "fonts": [
    {
      "name": "Helvetica",
      "type": "Type1",
      "action": "kept",
      "before": { "count": 1, "embedded": false, "subset": false, "bytes": 0 },
      "after": { "count": 1, "embedded": false, "subset": false, "bytes": 0 }
    }
  ],
  "images": [
    {
      "page": 1,
      "name": "Im0",
      "dpi": 64,
      "before": { "width": 500, "height": 400, "colorSpace": "DeviceRGB", "codec": "FlateDecode", "bytes": 600191 },
      "after": { "width": 500, "height": 400, "colorSpace": "DeviceRGB", "codec": "DCTDecode", "bytes": 76186 }
    }
  ],
  "cleanup": []
}
```

## Top level

| Name | Type | Description |
|---|---|---|
| `originalSize` | number | Input file size in bytes, as given (before decryption). |
| `compressedSize` | number | Output file size in bytes. |
| `fonts` | array of [font entries](#fonts) | One entry per font name and type in the input. |
| `images` | array of [image entries](#images) | One entry per image placed directly on a page of the input. |
| `cleanup` | array of [cleanup entries](#cleanup) | Counts of document structures before and after. |

With a password, "before" describes the decrypted input.

## fonts

Fonts with the same name and type are grouped into one entry, ordered by where the font first appears in the file's object list.

| Name | Type | Description |
|---|---|---|
| `name` | string | Font name without the six-letter subset prefix (`ABCDEF+`), for example `Helvetica`. `(unnamed)` when the font has no name. |
| `type` | string | `Type1`, `TrueType`, `Type3`, `MMType1`, or for composite fonts `Type0/CIDFontType0` or `Type0/CIDFontType2`. |
| `action` | string | What happened to the font. See below. |
| `before` | font state | The font in the input. |
| `after` | font state or `null` | The font in the output, or `null` when it is gone. |

A font state has these fields:

| Name | Type | Description |
|---|---|---|
| `count` | number | Font objects with this name and type. |
| `embedded` | boolean | `true` if any copy carries its font program in the file. Type3 fonts always count as embedded. |
| `subset` | boolean | `true` only if every copy is a subset (holds only the characters the document uses). |
| `bytes` | number | Combined stored size of the embedded font programs. |

`action` is the first of these that applies:

| Value | Meaning |
|---|---|
| `removed` | The font is no longer in the document, for example because the page content that used it was rasterized. |
| `unembedded` | It was embedded and now the viewer supplies it (`removeStandardFonts`, `unembedAliasedFonts`). |
| `subset` | It was a full font and is now a subset. |
| `merged` | Duplicate copies were merged into fewer (`mergeFonts`). |
| `recompressed` | Same font, stored in fewer bytes. |
| `kept` | Unchanged. |

## images

| Name | Type | Description |
|---|---|---|
| `page` | number | Page number, starting at 1. |
| `name` | string | The image's resource name on that page, for example `Im0`. |
| `dpi` | number or `null` | Resolution as placed on the page, as measured by the engine. `null` when it is not known. |
| `before` | image state | The image in the input. |
| `after` | image state or `null` | The image with the same name on the same page of the output, or `null` when it is gone, for example after rasterizing or MRC. |

An image state has these fields:

| Name | Type | Description |
|---|---|---|
| `width`, `height` | number | Size in pixels. |
| `colorSpace` | string | For example `DeviceRGB`, `DeviceGray`, `DeviceCMYK`, `ICCBased`, `Indexed`. `ImageMask` for a stencil mask, `none` when not set. |
| `codec` | string | The compression filter: `DCTDecode` (JPEG), `JPXDecode` (JPEG 2000), `JBIG2Decode`, `FlateDecode`, `CCITTFaxDecode` and so on. Several filters are joined with `+`. `none` means uncompressed. |
| `bytes` | number | Stored size of the image data. |

Images inside form XObjects (reusable page parts) are not listed.

## cleanup

Each entry is `{ "item": string, "before": number, "after": number }`. Only items present in the input or the output are listed, so an empty array means the document had none of them.

| `item` | What is counted |
|---|---|
| `documentInfo` | Entries in the document information dictionary (title, author, producer, dates, custom entries). |
| `xmpMetadata` | 1 when the document has an XMP metadata packet. |
| `bookmarks` | Bookmark (outline) entries. |
| `attachments` | Embedded files plus file attachment annotations. |
| `actions` | Open action, document and page triggers, document JavaScript, and annotations that run JavaScript, launch files, import data, submit forms or play media. |
| `namedDestinations` | Named destinations. |
| `printSettings` | Print settings in the viewer preferences (scaling, duplex, paper tray, page range, copies, print area, print clip). |
| `annotations` | Markup annotations: everything except form fields and links. |
| `formFields` | Form field widgets. |
| `links` | Link annotations. |
| `thumbnails` | Pages with an embedded thumbnail. |
| `structureTree` | 1 when the document is tagged. |
| `articleThreads` | 1 when the document has article threads. |
| `outputIntents` | 1 when the document has output intents (colour profiles for print). |
| `searchIndex` | Web capture identifiers (the `IDS` and `URLS` name trees). |

## Types

The Node.js SDK exports the report types by name:

```ts
import type {
  CompressResult,
  HyperReport,
  HyperFontReport,
  HyperImageReport,
  HyperImageState,
  HyperCleanupReport,
} from 'hyper-compress';

function largestImage(result: CompressResult): HyperImageReport | undefined {
  const report: HyperReport | undefined = result.report;
  return [...(report?.images ?? [])].sort((a, b) => b.before.bytes - a.before.bytes)[0];
}
```

```ts
interface HyperReport {
  originalSize: number;
  compressedSize: number;
  fonts: HyperFontReport[];
  images: HyperImageReport[];
  cleanup: HyperCleanupReport[];
}

interface HyperFontReport {
  name: string;
  type: string;
  before: { count: number; embedded: boolean; subset: boolean; bytes: number };
  after: { count: number; embedded: boolean; subset: boolean; bytes: number } | null;
  action: 'kept' | 'subset' | 'unembedded' | 'merged' | 'recompressed' | 'removed';
}

interface HyperImageReport {
  page: number;
  name: string;
  dpi: number | null;
  before: HyperImageState;
  after: HyperImageState | null;
}

interface HyperImageState {
  width: number;
  height: number;
  colorSpace: string;
  codec: string;
  bytes: number;
}

interface HyperCleanupReport {
  item: string;
  before: number;
  after: number;
}
```

In Rust the report is `hyper_compress::report::Report`; its fields are listed on [Rust crate](/rust#report).

## Next steps

- [Options](/options): the options behind each `action` and cleanup item.
- [Document structure](/guides/document-structure): what each cleanup removes and when to use it.
- [CLI](/cli) and [Node.js SDK](/node-sdk): how to request a report.
