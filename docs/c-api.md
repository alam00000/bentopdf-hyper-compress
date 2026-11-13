# C API

`libhypercompress` exposes the engine as a small, stable C ABI for embedding in any language that can call C. The shared library exports only the symbols below, and the ABI is additive only within a major version. Each release attaches a prebuilt `libhypercompress.dylib` for macOS arm64; other platforms build it from source with `sdk/native/build-dylib.sh`.

The C library is the engine pass alone. The [Node SDK](/node-sdk) adds a repack stage, the size target search, the "never larger" comparison, PDF/A detection and warnings on top of it. If you embed the C library, those parts are yours to do.

## The surface

```c
#include "hpdf.h"

int hpdf_compress_file(const char* in_path,
                       const char* out_path,
                       const int* option_ids,
                       const int* option_values,
                       int option_count,
                       const char* password);

unsigned char* hpdf_compress_buffer(const unsigned char* in_data,
                                    unsigned long in_size,
                                    const int* option_ids,
                                    const int* option_values,
                                    int option_count,
                                    const char* password,
                                    unsigned long* out_size,
                                    int* out_status);

void hpdf_free(unsigned char* buffer);
const char* hpdf_last_error(void);
const char* hpdf_version(void);
```

`hpdf.h` also defines `HPDF_VERSION`, the version as a string, for example `"1.1.4"`.

### hpdf_compress_file

Compresses path to path. Returns:

| Return | Meaning |
|---|---|
| `0` | compressed; the output is written |
| `2` | the input is digitally signed, so it was not compressed; the output is an unchanged copy |
| `1` | failed; `hpdf_last_error` says why, and the output may be missing |

### hpdf_compress_buffer

Compresses memory to memory. On success it returns a buffer you own, sets `*out_size`, and sets `*out_status` to `0` (compressed) or `2` (signed, unchanged copy). On failure it returns `NULL` and sets `*out_status` to `1`. Release the buffer with `hpdf_free`. Inputs over 2 GB are refused.

### The rest

- `hpdf_last_error` describes the most recent failure, or returns an empty string. The message is one of: `cannot open input (missing, corrupt, or wrong password)`, `cannot parse input (corrupt or wrong password)`, `engine pass failed`, `cannot open output for writing`, `save failed`, `input larger than 2 GB is not supported`.
- `hpdf_version` returns the engine version string.
- `password` may be `NULL` or empty for unencrypted input.
- The output is not guaranteed to be smaller than the input. Compare the sizes and keep the original when it is smaller.

## A complete example

```c
#include <stdio.h>
#include "hpdf.h"

int main(void) {
  /* the medium preset, from buildHyperTokens(HYPER_PRESETS.medium) */
  const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 6, 10, 13, 14, 16, 17, 20, 67};
  const int vals[] = {150, 50, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1, 8322, 6};
  const int n = sizeof ids / sizeof ids[0];

  int rc = hpdf_compress_file("in.pdf", "out.pdf", ids, vals, n, NULL);
  if (rc == 1) {
    fprintf(stderr, "compress failed: %s\n", hpdf_last_error());
    return 1;
  }
  if (rc == 2) printf("signed document, copied unchanged\n");
  printf("engine %s\n", hpdf_version());
  return 0;
}
```

Compile and run on macOS, from a repository checkout with the dylib in place:

```bash
clang demo.c -I sdk/native -L sdk/native/lib -lhypercompress \
  -Wl,-rpath,@executable_path/sdk/native/lib -o demo
./demo
```

## The presets as id and value pairs

| Preset | Pairs |
|---|---|
| `low` | `1=200 2=80 3=4 8=1 18=1 7=1 63=10 10=1 13=1 14=1 16=1 17=1 20=8322 67=6` |
| `medium` | `1=150 2=50 3=4 8=1 18=1 7=1 63=10 6=1 10=1 13=1 14=1 16=1 17=1 20=8322 67=6` |
| `high` | `1=72 59=150 2=20 3=4 8=1 18=1 7=1 63=10 6=1 10=1 12=1 9=1 13=1 14=1 16=1 17=1 20=565442 67=6` |
| `lossless` | `3=0 7=1 10=1 13=1 14=1 16=1 17=1 20=8322` |

For any other combination, let the Node SDK translate an [options object](/options) for you:

```bash
node -e "import('hyper-compress').then(m => console.log(m.buildHyperTokens(m.normalizeHyperOptions({ ...m.HYPER_PRESETS.medium, grayscale: true }))))"
```

## Every option id

Ids are defined in `core/include/fpdf_compress.h` and stable within a major version. An id you do not pass keeps the engine default shown here, which is not the same as the `medium` preset. Unknown ids are ignored.

### Images

| Id | Name | Engine default | Values |
|---|---|---|---|
| 1 | `IMAGE_MAX_DPI` | 0 | resolution limit for colour and gray images; 0 is no limit |
| 57 | `IMAGE_COLOR_MAX_DPI` | 0 | limit for colour images; 0 uses id 1 |
| 58 | `IMAGE_GRAY_MAX_DPI` | 0 | limit for gray images; 0 uses id 1 |
| 59 | `IMAGE_MONO_MAX_DPI` | 0 | limit for 1-bit images; 0 is no limit |
| 50 | `IMAGE_THRESHOLD_DPI` | 0 | only resample above this; 0 is the built-in rule |
| 52 | `IMAGE_RESAMPLE_QUALITY` | 1 | 0 nearest, 1 bilinear, 2 high |
| 2 | `IMAGE_QUALITY` | 75 | JPEG quality, 5 to 100 |
| 3 | `IMAGE_ENCODING` | 4 | 0 keep the original encoding (lossless), 1 JPEG, 2 Flate, 3 JPEG 2000, 4 automatic |
| 5 | `IMAGE_GRAYSCALE` | 0 | 1 converts images to gray |
| 51 | `IMAGE_COLOR_TARGET` | 0 | 0 original, 1 sRGB, 2 CMYK, 3 gray |
| 69 | `CONVERT_PAGE_COLORS` | 0 | 1 also converts text and vector colours to the colour target |
| 53 | `JPEG_SUBSAMPLE` | 0 | 0 encoder default, 1 4:2:0, 2 4:2:2, 3 4:4:4 |
| 54 | `JPEG_OPTIM_HUFFMAN` | 0 | 1 writes optimized Huffman tables |
| 55 | `JPEG_PROGRESSIVE` | 0 | 1 writes progressive JPEG |
| 18 | `IMAGE_PREFER_JPX` | 0 | 1 lets JPEG 2000 compete with JPEG per image |
| 6 | `CLIP_IMAGES` | 0 | 1 crops image data hidden outside the clip |
| 7 | `REDUCE_COLOR_COMPLEXITY` | 0 | 1 reduces colours where it does not change appearance |
| 63 | `IMAGE_LOSSY_INDEX` | 0 | with id 7, also tries lossy palette images, with this allowed error; 0 is off, the SDK uses 10 |
| 56 | `IMAGE_INDEX_MIN_DPI` | 150 | with id 7, only images placed at this resolution or more are tried as palette images |
| 70 | `SPEED` | 0 | 0 balanced, 1 quality, 2 fast |
| 8 | `FLATTEN_ICC` | 0 | 1 flattens ICC profiles |

### Scans

| Id | Name | Engine default | Values |
|---|---|---|---|
| 40 | `MRC_MODE` | 0 | 0 off, 1 auto, 2 force |
| 41 | `MRC_SELECTOR_DPI` | 300 | resolution cap of the MRC text mask |
| 42 | `MRC_BG_DPI` | 75 | background and ink layer resolution |
| 43 | `MRC_BG_QUALITY` | 30 | background JPEG quality |
| 44 | `MRC_FG_QUALITY` | 50 | ink layer JPEG quality |
| 72 | `MRC_DETECT_PHOTOS` | 1 | 0 turns off photo detection |
| 73 | `JBIG2_SYMBOLS` | 0 | 1 allows lossy JBIG2 symbol reuse |
| 60 | `CONVERT_TO_BITMAP` | 0 | 1 rasterizes every page unless the document has text, 2 rasterizes even with text |
| 61 | `CONVERT_TO_BITMAP_DPI` | 150 | rasterize resolution |
| 62 | `CONVERT_TO_BITMAP_QUALITY` | 50 | rasterize JPEG quality |

### Fonts and objects

| Id | Name | Engine default | Values |
|---|---|---|---|
| 10 | `FONT_SUBSET` | 1 | 0 keeps full fonts |
| 12 | `FONT_REMOVE_STANDARD` | 0 | 1 unembeds the standard 14 fonts |
| 9 | `UNEMBED_ALIASED_FONTS` | 0 | 1 unembeds clones of the standard fonts |
| 13 | `FONT_MERGE` | 0 | 1 merges duplicate font programs |
| 14 | `FONT_DEDUP_DICTS` | 0 | 1 merges duplicate font dictionaries |
| 16 | `DEDUP_OBJECTS` | 0 | 1 merges duplicate objects |
| 17 | `OPTIMIZE_RESOURCES` | 0 | 1 merges duplicate resource dictionaries |

### Content and output

| Id | Name | Engine default | Values |
|---|---|---|---|
| 30 | `RECOMPRESS_CONTENT_STREAMS` | 1 | 0 leaves page content streams as they are |
| 67 | `CONTENT_PRECISION` | 0 | 0 exact, or 3 to 8 significant digits; 1 and 2 become 3 |
| 64 | `PDFA_MODE` | 0 | 3 keeps a PDF/A file conformant |
| 65 | `STREAM_CODEC` | 0 | 1 uses PDF 2.0 Brotli streams |
| 66 | `BROTLI_QUALITY` | 11 | Brotli quality, 0 to 11 |
| 68 | `THREADS` | 0 | threads per compression; 0 uses the machine's cores, up to 8 |
| 71 | `REPORT` | 0 | 1 prints per-image notes to stderr for the report |

### Id 20: what to remove or flatten

Id 20 is a bit mask. Add the values of everything you want:

| Bit | Removes or flattens | Options name |
|---|---|---|
| `0x1` | structure tree (tags) | `removeStructTree` |
| `0x2` | page thumbnails | `removeThumbnails` |
| `0x4` | bookmarks | `removeBookmarks` |
| `0x8` | all annotations | |
| `0x10` | form fields | |
| `0x20` | attachments | `removeAttachments` |
| `0x40` | alternate images | `removeAlternates` |
| `0x80` | document piece info | `removeAppData` (with `0x2000`) |
| `0x100` | document info and XMP metadata | `removeMetadata` |
| `0x200` | print settings | `removePrintSettings` |
| `0x400` | web capture search index | `removeSearchIndex` |
| `0x800` | named destinations | `removeNamedDests` |
| `0x1000` | actions and JavaScript | `removeActions` |
| `0x2000` | page piece info | `removeAppData` (with `0x80`) |
| `0x4000` | output intents | `removeOutputIntents` |
| `0x8000` | article threads | `removeThreads` |
| `0x10000` | appearances of empty signature fields | `removeSignatureAppearances` |
| `0x20000` | links | |
| `0x40000` | flatten markup annotations, keep forms and links | `annotations: 'flatten'` |
| `0x80000` | web capture info | `removeSpiderInfo` |
| `0x100000` | flatten form fields | `flattenForms` |
| `0x200000` | flatten links | `flattenLinks` |
| `0x400000` | digital signatures | `removeSignatures` |
| `0x800000` | markup annotations, keep forms and links | `annotations: 'remove'` |

Bits without an options name are only reachable from the C API and the worker binary.

## What holds, and what does not

These [guarantees](/guarantees) live inside the library and hold for every embedder: signed documents are not changed, `PDFA_MODE` keeps PDF/A conformant, and rasterizing refuses pages with text unless you pass `CONVERT_TO_BITMAP=2`.

These live in the SDK and are not in the library: the output being never larger than the input, the size target search, PDF/A detection and warnings.

The C API also gives you no process isolation; the engine runs inside your process. If your input is untrusted, run the worker binary instead.

## The worker binary

`hpdf-worker` is the engine as a command, the same one the Node SDK runs for every compression:

```bash
hpdf-worker in.pdf out.pdf 1=150 2=50 3=4 20=8322
```

It takes the input path, the output path and any number of `id=value` pairs; anything else is ignored. It has no password option, so decrypt encrypted files first (the SDK uses `qpdf --decrypt`).

| Exit code | Meaning |
|---|---|
| 0 | success, including a signed input written unchanged |
| 1 | the engine pass failed |
| 2 | wrong arguments |
| 3 | the input could not be opened |
| 4 | the output could not be written |

On stderr it prints `SIGNED_SKIP` when a signed input was left unchanged, `SIGNATURES_REMOVED` when id 20 bit `0x400000` stripped signatures, and `[hyper] rasterize skipped: document has a text layer` when rasterizing was refused. A crash or hang ends only that process, which is why the SDK uses it for untrusted files.
