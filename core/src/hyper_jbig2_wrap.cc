#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>

extern "C" {
struct Pix;
}
#include "third_party/leptonica/src/allheaders.h"
#include "third_party/jbig2enc/src/jbig2enc.h"

namespace {

bool CopyBytesIntoPix(struct Pix* dst,
                       const uint8_t* src_data,
                       int src_stride,
                       int width,
                       int height) {
  if (!dst || !src_data || width <= 0 || height <= 0) return false;
  l_uint32* pix_data = pixGetData(dst);
  if (!pix_data) return false;
  const int wpl = pixGetWpl(dst);
  const int dst_bytes_per_line = wpl * 4;
  const int copy_bytes = (width + 7) / 8;
  for (int y = 0; y < height; ++y) {
    uint8_t* dst_row = reinterpret_cast<uint8_t*>(pix_data) +
                        y * dst_bytes_per_line;
    const uint8_t* src_row = src_data + y * src_stride;

    memcpy(dst_row, src_row, copy_bytes);
  }

  pixEndianByteSwap(dst);
  return true;
}

}

extern "C" {

__attribute__((visibility("default")))
bool HyperJbig2EncodeMono(const uint8_t* src_data,
                          int src_stride,
                          int width,
                          int height,
                          int xres,
                          int yres,
                          uint8_t** out_buf,
                          size_t* out_len) {
  if (!src_data || !out_buf || !out_len) return false;
  if (width <= 0 || height <= 0 || src_stride <= 0) return false;
  if (xres <= 0) xres = 300;
  if (yres <= 0) yres = 300;

  struct Pix* pix = pixCreate(width, height, 1);
  if (!pix) return false;
  if (!CopyBytesIntoPix(pix, src_data, src_stride, width, height)) {
    pixDestroy(&pix);
    return false;
  }
  pixSetResolution(pix, xres, yres);

  int length = 0;
  uint8_t* encoded = jbig2_encode_generic(
      pix,
       false,
      xres, yres,
       false,
      &length);
  pixDestroy(&pix);
  if (!encoded || length <= 0) {
    if (encoded) free(encoded);
    return false;
  }
  *out_buf = encoded;
  *out_len = static_cast<size_t>(length);
  return true;
}

__attribute__((visibility("default")))
void HyperJbig2Free(void* buf) {
  if (buf) free(buf);
}

struct HyperJbig2Doc {
  struct jbig2ctx* j;

  std::vector<struct Pix*> pixes;
};

__attribute__((visibility("default")))
void* HyperJbig2BeginDoc(void) {

  struct jbig2ctx* j =
      jbig2_init(0.80f, 0.5f,  300,  300,
                  false,  -1);
  if (!j) return nullptr;
  auto* doc = new (std::nothrow) HyperJbig2Doc();
  if (!doc) {
    jbig2_destroy(j);
    return nullptr;
  }
  doc->j = j;
  return doc;
}

__attribute__((visibility("default")))
bool HyperJbig2AddPage(void* ctx,
                       const uint8_t* src_data,
                       int src_stride,
                       int width,
                       int height) {
  auto* doc = static_cast<HyperJbig2Doc*>(ctx);
  if (!doc || !doc->j || !src_data || width <= 0 || height <= 0 ||
      src_stride <= 0) {
    return false;
  }
  struct Pix* pix = pixCreate(width, height, 1);
  if (!pix) return false;
  if (!CopyBytesIntoPix(pix, src_data, src_stride, width, height)) {
    pixDestroy(&pix);
    return false;
  }
  pixSetResolution(pix, 300, 300);
  jbig2_add_page(doc->j, pix);

  doc->pixes.push_back(pix);
  return true;
}

__attribute__((visibility("default")))
bool HyperJbig2FinishDoc(void* ctx,
                         uint8_t** out_globals,
                         size_t* out_globals_len) {
  auto* doc = static_cast<HyperJbig2Doc*>(ctx);
  if (!doc || !doc->j || !out_globals || !out_globals_len) return false;
  int length = 0;
  uint8_t* globals = jbig2_pages_complete(doc->j, &length);
  if (!globals || length <= 0) {
    if (globals) free(globals);
    return false;
  }
  *out_globals = globals;
  *out_globals_len = static_cast<size_t>(length);
  return true;
}

__attribute__((visibility("default")))
bool HyperJbig2GetPage(void* ctx,
                       int page_index,
                       uint8_t** out_buf,
                       size_t* out_len) {
  auto* doc = static_cast<HyperJbig2Doc*>(ctx);
  if (!doc || !doc->j || !out_buf || !out_len || page_index < 0) {
    return false;
  }
  int length = 0;
  uint8_t* page_bytes = jbig2_produce_page(
      doc->j, page_index,  -1,  -1, &length);
  if (!page_bytes || length <= 0) {
    if (page_bytes) free(page_bytes);
    return false;
  }
  *out_buf = page_bytes;
  *out_len = static_cast<size_t>(length);
  return true;
}

__attribute__((visibility("default")))
void HyperJbig2EndDoc(void* ctx) {
  auto* doc = static_cast<HyperJbig2Doc*>(ctx);
  if (!doc) return;
  if (doc->j) jbig2_destroy(doc->j);
  for (auto* pix : doc->pixes) {
    struct Pix* p = pix;
    pixDestroy(&p);
  }
  delete doc;
}

__attribute__((visibility("default")))
bool HyperQuantizeRgbToIndexed(const uint8_t* bgra,
                               int width,
                               int height,
                               int src_stride,
                               int bpp,
                               int max_colors,
                               uint8_t** out_indices,
                               uint8_t** out_palette,
                               int* out_ncolors,
                               double* out_mean_err) {
  if (!bgra || !out_indices || !out_palette || !out_ncolors ||
      !out_mean_err || width <= 0 || height <= 0 || src_stride <= 0)
    return false;
  if (bpp != 24 && bpp != 32) return false;
  if (max_colors < 2) max_colors = 2;
  if (max_colors > 256) max_colors = 256;

  struct Pix* pixs = pixCreate(width, height, 32);
  if (!pixs) return false;
  const int step = bpp == 24 ? 3 : 4;
  l_uint32* sdata = pixGetData(pixs);
  const int swpl = pixGetWpl(pixs);
  for (int y = 0; y < height; ++y) {
    const uint8_t* row = bgra + static_cast<size_t>(y) * src_stride;
    l_uint32* line = sdata + static_cast<size_t>(y) * swpl;
    for (int x = 0; x < width; ++x) {
      const uint8_t* p = row + x * step;
      composeRGBPixel(p[2], p[1], p[0], &line[x]);
    }
  }

  struct Pix* pixd =
      pixMedianCutQuantGeneral(pixs,  0,  8,
                               max_colors,  0,  1,
                                1);
  if (!pixd) {
    pixDestroy(&pixs);
    return false;
  }
  struct PixColormap* cmap = pixGetColormap(pixd);
  if (!cmap) {
    pixDestroy(&pixs);
    pixDestroy(&pixd);
    return false;
  }
  const int ncolors = pixcmapGetCount(cmap);
  if (ncolors < 1 || ncolors > 256) {
    pixDestroy(&pixs);
    pixDestroy(&pixd);
    return false;
  }

  uint8_t* palette =
      static_cast<uint8_t*>(malloc(static_cast<size_t>(ncolors) * 3));
  if (!palette) {
    pixDestroy(&pixs);
    pixDestroy(&pixd);
    return false;
  }
  for (int i = 0; i < ncolors; ++i) {
    l_int32 r = 0, g = 0, b = 0;
    pixcmapGetColor(cmap, i, &r, &g, &b);
    palette[i * 3 + 0] = static_cast<uint8_t>(r);
    palette[i * 3 + 1] = static_cast<uint8_t>(g);
    palette[i * 3 + 2] = static_cast<uint8_t>(b);
  }

  const size_t npix = static_cast<size_t>(width) * height;
  uint8_t* indices = static_cast<uint8_t*>(malloc(npix));
  if (!indices) {
    free(palette);
    pixDestroy(&pixs);
    pixDestroy(&pixd);
    return false;
  }
  l_uint32* ddata = pixGetData(pixd);
  const int dwpl = pixGetWpl(pixd);
  uint64_t err_sum = 0;
  size_t ip = 0;
  for (int y = 0; y < height; ++y) {
    const uint8_t* row = bgra + static_cast<size_t>(y) * src_stride;
    l_uint32* line = ddata + static_cast<size_t>(y) * dwpl;
    for (int x = 0; x < width; ++x) {
      const int idx = GET_DATA_BYTE(line, x);
      const uint8_t safe_idx =
          static_cast<uint8_t>(idx < ncolors ? idx : 0);
      indices[ip++] = safe_idx;
      const uint8_t* p = row + x * step;
      const int dr = static_cast<int>(palette[safe_idx * 3 + 0]) - p[2];
      const int dg = static_cast<int>(palette[safe_idx * 3 + 1]) - p[1];
      const int db = static_cast<int>(palette[safe_idx * 3 + 2]) - p[0];
      err_sum += static_cast<uint64_t>(dr < 0 ? -dr : dr);
      err_sum += static_cast<uint64_t>(dg < 0 ? -dg : dg);
      err_sum += static_cast<uint64_t>(db < 0 ? -db : db);
    }
  }
  *out_mean_err =
      npix ? static_cast<double>(err_sum) / (static_cast<double>(npix) * 3.0)
           : 0.0;
  *out_indices = indices;
  *out_palette = palette;
  *out_ncolors = ncolors;
  pixDestroy(&pixs);
  pixDestroy(&pixd);
  return true;
}

}
