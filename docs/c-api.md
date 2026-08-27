# C API

`libhypercompress` exposes the engine as a small, stable C ABI for embedding in any language that can call C. The shared library exports only the symbols below, and the ABI is additive only within a major version. Each release attaches a prebuilt `libhypercompress.dylib` for macOS arm64; other platforms build it from source with `sdk/native/build-dylib.sh`.

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

- `hpdf_compress_file` compresses path to path and returns 0 on success.
- `hpdf_compress_buffer` compresses memory to memory. The returned buffer is owned by you; release it with `hpdf_free`. A null return means failure, and `out_status` carries the engine status.
- `hpdf_last_error` describes the most recent failure.
- `hpdf_version` returns the engine version string.
- `password` may be `NULL` for unencrypted input.

## A complete example

```c
#include <stdio.h>
#include "hpdf.h"

int main(void) {
  const int ids[]  = {2, 1};    /* 2 = image quality, 1 = max dpi */
  const int vals[] = {50, 150}; /* the medium preset's core values */

  int rc = hpdf_compress_file("in.pdf", "out.pdf", ids, vals, 2, NULL);
  if (rc != 0) {
    fprintf(stderr, "compress failed: %s\n", hpdf_last_error());
    return 1;
  }
  printf("compressed with engine %s\n", hpdf_version());
  return 0;
}
```

Compile and run on macOS, from a repository checkout with the dylib in place:

```bash
clang demo.c -I sdk/native -L sdk/native/lib -lhypercompress \
  -Wl,-rpath,@executable_path/sdk/native/lib -o demo
./demo
```

## Options as id and value pairs

Options are passed as parallel arrays of numeric id and value pairs; these are the same low-level tokens every higher-level surface generates. The two easiest ways to get correct pairs:

1. Ask the Node SDK. `buildHyperTokens` turns any [options object](/options) into `id=value` tokens you can transcribe:

   ```bash
   node -e "import('hyper-compress').then(m => console.log(m.buildHyperTokens(m.HYPER_PRESETS.medium)))"
   ```

2. Read the id definitions in `core/include/fpdf_compress.h`; they are stable within a major version.

## What holds, and what does not

The [engine guarantees](/guarantees) that live inside the library hold for every embedder: monotonic output, signed-document passthrough, PDF/A honesty, the rasterize text guard. What the C API does not give you is process isolation; the engine runs inside your process. If your input is untrusted, spawn the `hpdf-worker` binary instead and feed it paths and tokens on the command line, which is exactly what the Node SDK does to contain hostile files.
