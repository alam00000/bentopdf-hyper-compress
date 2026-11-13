# Compression report

The report shows what happened inside one compression: each font, each page image and each piece of document structure, before and after. Use it to explain a result to a user, to audit what a preset removes, or to find which images make a file large.

The CLI and the Node SDK can produce it. The WebAssembly SDK and the HTTP API cannot.

```bash
hyper in.pdf out.pdf --report report.json
```

```js
import { compress } from 'hyper-compress';

const r = await compress({ sourcePath: 'in.pdf', savePath: 'out.pdf', report: true });
console.log(r.report.images);
```

Pass `report: true` to get the report on the result, or a file path (`report: 'report.json'`) to also write it to disk as JSON. Making a report adds two read passes over the input and the output, so leave it off when you do not need it.

If the PDF structure cannot be read, the compression still succeeds, `report` is left out, and the warning `report unavailable: the PDF structure could not be read` is added.

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
      "after":  { "count": 1, "embedded": false, "subset": false, "bytes": 0 }
    }
  ],
  "images": [
    {
      "page": 1,
      "name": "Im0",
      "dpi": 64,
      "before": { "width": 500, "height": 400, "colorSpace": "DeviceRGB", "codec": "FlateDecode", "bytes": 600191 },
      "after":  { "width": 500, "height": 400, "colorSpace": "DeviceRGB", "codec": "DCTDecode", "bytes": 76186 }
    }
  ],
  "cleanup": []
}
```

## Top level

| Field | Type | Meaning |
|---|---|---|
| `originalSize` | number | input size in bytes |
| `compressedSize` | number | output size in bytes |
| `fonts` | array | one entry per font in the input |
| `images` | array | one entry per image placed directly on a page |
| `cleanup` | array | counts of document structures before and after |

## fonts

Fonts with the same name and type are grouped into one entry.

| Field | Meaning |
|---|---|
| `name` | font name without the six letter subset prefix, for example `Helvetica`, or `(unnamed)` |
| `type` | font type: `Type1`, `TrueType`, `Type3`, `MMType1`, or for composite fonts `Type0/CIDFontType0` or `Type0/CIDFontType2` |
| `before`, `after` | `count` (font objects with this name and type), `embedded` (true if any copy is embedded; Type3 fonts always count as embedded), `subset` (true only if every copy is a subset) and `bytes` (combined size of the embedded font programs). `after` is `null` when the font is gone. |
| `action` | what happened, see below |

| `action` | Meaning |
|---|---|
| `kept` | unchanged |
| `subset` | was a full font, now holds only the glyphs the document uses |
| `unembedded` | was embedded, now supplied by the viewer (`removeStandardFonts`, `unembedAliasedFonts`) |
| `merged` | duplicate copies were merged into fewer (`mergeFonts`) |
| `recompressed` | same font, stored in fewer bytes |
| `removed` | no longer in the document, for example because its only page content was rasterized |

## images

| Field | Meaning |
|---|---|
| `page` | page number, starting at 1 |
| `name` | the image's resource name on that page, for example `Im0` |
| `dpi` | resolution as placed on the page, or `null` when it is not known |
| `before`, `after` | `width` and `height` in pixels, `colorSpace` (for example `DeviceRGB`, `DeviceGray`, `ICCBased`, `Indexed`), `codec` (the stream filter, for example `DCTDecode` for JPEG, `JPXDecode` for JPEG 2000, `JBIG2Decode`, `FlateDecode`; several filters are joined with `+`, and `none` means uncompressed) and `bytes`. A stencil mask reports `ImageMask` as its colour space. `after` is `null` when the image is gone, for example after rasterizing or MRC. |

Images inside form XObjects are not listed.

## cleanup

Each entry is `{ "item": string, "before": number, "after": number }`. Only items present in the input or the output are listed, so an empty array means the document had none of them.

| `item` | What is counted |
|---|---|
| `documentInfo` | entries in the document information dictionary (title, author, producer, dates, custom entries) |
| `xmpMetadata` | 1 when the document has an XMP metadata packet |
| `bookmarks` | bookmark (outline) entries |
| `attachments` | embedded files plus file attachment annotations |
| `actions` | open action, document and page triggers, document JavaScript, and annotations that run JavaScript, launch, import, submit or play media |
| `namedDestinations` | named destinations |
| `printSettings` | print presets in the viewer preferences (scaling, duplex, tray, page range, copies, print area, print clip) |
| `annotations` | markup annotations: everything except form fields and links |
| `formFields` | form field widgets |
| `links` | link annotations |
| `thumbnails` | pages with an embedded thumbnail |
| `structureTree` | 1 when the document is tagged |
| `articleThreads` | 1 when the document has article threads |
| `outputIntents` | 1 when the document has output intents |
| `searchIndex` | web capture identifiers (the `IDS` and `URLS` name trees) |

## Types

The Node SDK exports the type of the report on the result as `CompressResult['report']`:

```ts
interface HyperReport {
  originalSize: number;
  compressedSize: number;
  fonts: HyperFontReport[];
  images: HyperImageReport[];
  cleanup: { item: string; before: number; after: number }[];
}
```
