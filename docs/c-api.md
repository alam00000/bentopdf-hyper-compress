---
title: C API
description: Embed the Hyper Compress engine in C, C++ or any language that can call C, with every function, option id and return code.
---

# C API

`libhypercompress` is the Hyper Compress engine as a small C library, for C, C++ and any language that can call C. It exports exactly five functions, and new versions only add to them within a major version.

Each [release](https://github.com/alam00000/bentopdf-hyper-compress/releases) attaches a prebuilt `libhypercompress.dylib` for macOS Apple silicon in `libhypercompress-<version>-macos-arm64.tar.gz`. The header is [`sdk/native/hpdf.h`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/sdk/native/hpdf.h) in the repository.

```c
#include "hpdf.h"
```

## What the library leaves to you

The C library is the engine pass alone. The other surfaces ([Node.js SDK](/node-sdk), [Rust crate](/rust), [CLI](/cli), [HTTP API](/http-api)) add these on top, and with the C library they are yours to do:

- Keeping the original when the output is not smaller.
- The [size target](/guides/size-targets) search.
- The repack pass with qpdf that merges objects into compressed object streams.
- PDF/A detection, and [warnings](/warnings) for options skipped for safety.
- Running each file in a separate process, so a crash or hang cannot take down your program.

If your input is untrusted, run the [worker program](#the-worker-program) in a child process instead of calling the library in your own process.

## Functions

```c
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

The library is safe to call from any number of threads. It starts the engine once on first use, and runs one compression at a time: calls made at the same moment from other threads wait their turn, so extra threads add safety but not speed. For several compressions at once, run several processes, or use the CLI, Node.js SDK or HTTP service, which run each job in its own engine process. `hpdf_last_error` returns the message of the calling thread's last call.

### hpdf_compress_file

Compresses the PDF at one path and writes the result to another.

| Name | Type | Description |
|---|---|---|
| `in_path` | `const char*` | Path of the input PDF. |
| `out_path` | `const char*` | Path to write. An existing file is replaced. |
| `option_ids` | `const int*` | Option ids, see [Option ids](#option-ids). |
| `option_values` | `const int*` | One value for each id, in the same order. |
| `option_count` | `int` | Number of id and value pairs. `0` uses the engine defaults. |
| `password` | `const char*` | Password for an encrypted input. `NULL` or `""` for none. |

| Returns | Meaning |
|---|---|
| `0` | Compressed. The output is written. |
| `2` | The input is digitally signed, so it was not compressed. The output is an unchanged copy, and the signature stays valid. |
| `1` | Failed. `hpdf_last_error()` says why. The output may be missing or incomplete. |

### hpdf_compress_buffer

Compresses a PDF in memory and returns the result in a new buffer.

| Name | Type | Description |
|---|---|---|
| `in_data` | `const unsigned char*` | The input PDF bytes. |
| `in_size` | `unsigned long` | Number of input bytes. At most 2 GB (`INT_MAX` bytes). |
| `option_ids` | `const int*` | Option ids. |
| `option_values` | `const int*` | One value for each id. |
| `option_count` | `int` | Number of pairs. |
| `password` | `const char*` | Password for an encrypted input, or `NULL`. |
| `out_size` | `unsigned long*` | Set to the output size on success, `0` on failure. May be `NULL`. |
| `out_status` | `int*` | Set to `0` (compressed), `2` (signed, unchanged copy) or `1` (failed). May be `NULL`. |

| Returns | Meaning |
|---|---|
| a buffer | The output PDF. You own it; release it with `hpdf_free`. |
| `NULL` | Failed. `hpdf_last_error()` says why. |

### hpdf_free

Releases a buffer returned by `hpdf_compress_buffer`. Passing `NULL` is safe.

### hpdf_last_error

Returns the message of the calling thread's most recent failure, or `""` after a compress call that succeeded. Each thread has its own message. The string belongs to the library and changes on that thread's next call.

| Message | Cause |
|---|---|
| `cannot open input (missing, corrupt, or wrong password)` | `hpdf_compress_file` could not open the input. |
| `cannot parse input (corrupt or wrong password)` | `hpdf_compress_buffer` could not parse the input. |
| `input larger than 2 GB is not supported` | `hpdf_compress_buffer` got more than `INT_MAX` bytes. |
| `engine pass failed` | The engine could not process the document. |
| `cannot open output for writing` | `hpdf_compress_file` could not create `out_path`. |
| `save failed` | Writing the output failed. |

### hpdf_version

Returns the library version, the same string as `HPDF_VERSION`.

## Example

This program compresses `input.pdf` with the `medium` preset. The id and value lists are what the Node.js SDK sends to the engine for `medium` (see [Presets as id and value pairs](#presets-as-id-and-value-pairs)).

```c
#include <stdio.h>
#include "hpdf.h"

int main(void) {
  const int ids[]  = {1, 2, 3, 8, 18, 7, 63, 6, 10, 13, 14, 16, 17, 20, 67};
  const int vals[] = {150, 50, 4, 1, 1, 1, 10, 1, 1, 1, 1, 1, 1, 8322, 6};
  const int n = sizeof ids / sizeof ids[0];

  int rc = hpdf_compress_file("input.pdf", "output.pdf", ids, vals, n, NULL);
  if (rc == 1) {
    fprintf(stderr, "compress failed: %s\n", hpdf_last_error());
    return 1;
  }
  if (rc == 2) printf("signed document, copied unchanged\n");
  printf("engine %s\n", hpdf_version());
  return 0;
}
```

Save it as `demo.c` and build it on macOS from the root of a repository checkout, with the library in `sdk/native/lib` (from the release file, or from `make build`):

```bash
clang demo.c -I sdk/native -L sdk/native/lib -lhypercompress \
  -Wl,-rpath,@executable_path/sdk/native/lib -o demo
./demo
```

```text
engine 1.1.4
```

The output is not guaranteed to be smaller than the input. Compare the sizes and keep the original when it is not smaller.

## Presets as id and value pairs

| Preset | Pairs |
|---|---|
| `low` | `1=200 2=80 3=4 8=1 18=1 7=1 63=10 10=1 13=1 14=1 16=1 17=1 20=8322 67=6` |
| `medium` | `1=150 2=50 3=4 8=1 18=1 7=1 63=10 6=1 10=1 13=1 14=1 16=1 17=1 20=8322 67=6` |
| `high` | `1=72 59=150 2=20 3=4 8=1 18=1 7=1 63=10 6=1 10=1 12=1 9=1 13=1 14=1 16=1 17=1 20=565442 67=6` |
| `lossless` | `3=0 7=1 10=1 13=1 14=1 16=1 17=1 20=8322` |

For any other combination, let the Node.js SDK translate an [options object](/options):

```bash
node -e "import('hyper-compress').then(m => console.log(m.buildHyperTokens(m.normalizeHyperOptions({ ...m.HYPER_PRESETS.medium, grayscale: true }))))"
```

## Option ids

Ids are defined in [`core/include/fpdf_compress.h`](https://github.com/alam00000/bentopdf-hyper-compress/blob/main/core/include/fpdf_compress.h) and stay the same within a major version. An id you do not pass keeps the engine default shown here, which is not the same as the `medium` preset. Unknown ids are ignored. If you pass an id twice, the last value wins.

### Images

| Id | Name | Engine default | Values |
|---|---|---|---|
| 1 | `IMAGE_MAX_DPI` | `0` | Resolution limit for colour and gray images. `0` is no limit. |
| 57 | `IMAGE_COLOR_MAX_DPI` | `0` | Limit for colour images. `0` uses id 1. |
| 58 | `IMAGE_GRAY_MAX_DPI` | `0` | Limit for gray images. `0` uses id 1. |
| 59 | `IMAGE_MONO_MAX_DPI` | `0` | Limit for 1-bit (black and white) images. `0` is no limit. |
| 50 | `IMAGE_THRESHOLD_DPI` | `0` | Only resample images above this resolution. `0` uses the built-in rule. |
| 52 | `IMAGE_RESAMPLE_QUALITY` | `1` | `0` nearest, `1` bilinear, `2` high. |
| 2 | `IMAGE_QUALITY` | `75` | JPEG quality, 5 to 100. |
| 3 | `IMAGE_ENCODING` | `4` | `0` keep the original encoding (lossless), `1` JPEG, `2` Flate, `3` JPEG 2000, `4` automatic. |
| 5 | `IMAGE_GRAYSCALE` | `0` | `1` converts images to gray. |
| 51 | `IMAGE_COLOR_TARGET` | `0` | `0` original, `1` sRGB, `2` CMYK, `3` gray. |
| 69 | `CONVERT_PAGE_COLORS` | `0` | `1` also converts text and vector colours to the colour target. |
| 53 | `JPEG_SUBSAMPLE` | `0` | `0` encoder default, `1` 4:2:0, `2` 4:2:2, `3` 4:4:4. |
| 54 | `JPEG_OPTIM_HUFFMAN` | `0` | `1` writes optimized Huffman tables. |
| 55 | `JPEG_PROGRESSIVE` | `0` | `1` writes progressive JPEG. |
| 18 | `IMAGE_PREFER_JPX` | `0` | `1` lets JPEG 2000 compete with JPEG for each image. |
| 6 | `CLIP_IMAGES` | `0` | `1` crops image data hidden outside the clip. |
| 7 | `REDUCE_COLOR_COMPLEXITY` | `0` | `1` reduces colours where it does not change appearance. |
| 63 | `IMAGE_LOSSY_INDEX` | `0` | With id 7, also tries lossy palette images with this allowed error. `0` is off; the SDK uses `10`. |
| 56 | `IMAGE_INDEX_MIN_DPI` | `150` | With id 7, only images placed at this resolution or more are tried as palette images. |
| 70 | `SPEED` | `0` | `0` balanced, `1` quality, `2` fast. |
| 8 | `FLATTEN_ICC` | `0` | `1` flattens ICC colour profiles. |

### Scans

| Id | Name | Engine default | Values |
|---|---|---|---|
| 40 | `MRC_MODE` | `0` | Mixed raster content, which splits scans into text and background layers. `0` off, `1` auto, `2` force. |
| 41 | `MRC_SELECTOR_DPI` | `300` | Resolution limit of the MRC text mask. |
| 42 | `MRC_BG_DPI` | `75` | Background and ink layer resolution. |
| 43 | `MRC_BG_QUALITY` | `30` | Background JPEG quality. |
| 44 | `MRC_FG_QUALITY` | `50` | Ink layer JPEG quality. |
| 72 | `MRC_DETECT_PHOTOS` | `1` | `0` turns off photo detection. |
| 73 | `JBIG2_SYMBOLS` | `0` | `1` allows lossy JBIG2 symbol reuse. |
| 60 | `CONVERT_TO_BITMAP` | `0` | `1` rasterizes every page unless the document has text, `2` rasterizes even with text. |
| 61 | `CONVERT_TO_BITMAP_DPI` | `150` | Rasterize resolution. Values under 36 become 36. |
| 62 | `CONVERT_TO_BITMAP_QUALITY` | `50` | Rasterize JPEG quality. |

### Fonts and objects

| Id | Name | Engine default | Values |
|---|---|---|---|
| 10 | `FONT_SUBSET` | `1` | `0` keeps full fonts. |
| 12 | `FONT_REMOVE_STANDARD` | `0` | `1` unembeds the standard 14 fonts. |
| 9 | `UNEMBED_ALIASED_FONTS` | `0` | `1` unembeds copies of the standard fonts. |
| 13 | `FONT_MERGE` | `0` | `1` merges duplicate font programs. |
| 14 | `FONT_DEDUP_DICTS` | `0` | `1` merges duplicate font dictionaries. |
| 16 | `DEDUP_OBJECTS` | `0` | `1` merges duplicate objects. |
| 17 | `OPTIMIZE_RESOURCES` | `0` | `1` merges duplicate resource dictionaries. |

### Content and output

| Id | Name | Engine default | Values |
|---|---|---|---|
| 30 | `RECOMPRESS_CONTENT_STREAMS` | `1` | `0` leaves page content streams as they are. |
| 67 | `CONTENT_PRECISION` | `0` | `0` exact, or 3 to 8 significant digits. `1` and `2` become `3`; values over 8 become 8. |
| 64 | `PDFA_MODE` | `0` | `3` keeps a PDF/A file conformant. |
| 65 | `STREAM_CODEC` | `0` | `1` uses PDF 2.0 Brotli streams. |
| 66 | `BROTLI_QUALITY` | `11` | Brotli quality, 1 to 11. |
| 68 | `THREADS` | `0` | Threads for one compression. `0` uses the machine's cores, up to 8. At most 64. |
| 71 | `REPORT` | `0` | `1` prints notes for each image to stderr, for the [report](/report). |

### Id 20: what to remove or flatten

Id 20 is a bit mask. Add the values of everything you want to remove or flatten:

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

Bits without an options name are only reachable from the C API and the worker program. For example, `8322` (`0x2000 + 0x80 + 0x2`) removes page and document piece info and page thumbnails.

## What the library guarantees

These [guarantees](/guarantees) live inside the library and hold for every program that embeds it:

- Signed documents are not changed, unless you set bit `0x400000` of id 20.
- `PDFA_MODE=3` keeps a PDF/A file conformant.
- Rasterizing (id 60 set to `1`) skips documents that have text. Only `2` rasterizes them.

The output never being larger than the input, the size target search, PDF/A detection and warnings live in the other surfaces, not in the library.

## The worker program

`hpdf-worker` is the engine as a command, the same one the Node.js SDK, the Rust crate, the CLI and the HTTP service run for every file. It ships in every release file and npm platform package.

```bash
hpdf-worker input.pdf output.pdf 1=150 2=50 3=4 20=8322
```

It takes the input path, the output path and any number of `id=value` pairs. Arguments that are not `id=value` pairs are ignored. It has no password option, so decrypt encrypted files first; the SDKs run `qpdf --decrypt`.

| Exit code | Meaning |
|---|---|
| `0` | Success, including a signed input written unchanged. |
| `1` | The engine pass failed. |
| `2` | Fewer than two paths were given. |
| `3` | The input could not be opened, for example because it is missing, damaged or encrypted. |
| `4` | The output could not be created. |

What it prints on stderr:

| Line starts with | Meaning |
|---|---|
| `SIGNED_SKIP` | The input is signed and was written unchanged. |
| `SIGNATURES_REMOVED` | Bit `0x400000` of id 20 removed the signatures. |
| `[hyper] rasterize skipped: document has a text layer` | Id 60 was `1` and the document has text, so no page was rasterized. |

A crash or hang ends only that process, which is why the other surfaces use it for every file.

## Build the library

`sdk/native/build-dylib.sh` builds `sdk/native/lib/libhypercompress.dylib` on macOS, exporting only the five `hpdf_*` functions; `make build` runs it after the engine build. The script is macOS only. On other platforms, compile `sdk/native/hpdf_wrap.c` and link it with the engine archive and jpegli as `sdk/native/build-dylib.sh` does. [Building from source](/building) covers the engine build.

## Next steps

- [Options](/options): what each setting does, by its options name.
- [Guarantees](/guarantees): what the engine never does to your files.
- [Building from source](/building): build the engine and the library.
- [Platforms](/platforms): where each surface runs.
