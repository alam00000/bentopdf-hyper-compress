#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "lib/jpegli/encode.h"
#include "lib/jpegli/common.h"

namespace {

struct HyperJpegliErr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

void HyperJpegliErrorExit(j_common_ptr cinfo) {
  auto* err = reinterpret_cast<HyperJpegliErr*>(cinfo->err);
  longjmp(err->setjmp_buffer, 1);
}

}

extern "C" {

__attribute__((visibility("default")))
bool HyperJpegliEncode(const uint8_t* src,
                       int width,
                       int height,
                       int stride,
                       int components_in,
                       int is_bgr,
                       int quality,
                       int progressive,
                       int subsample420,
                       uint8_t** out_buf,
                       size_t* out_len) {
  if (!src || width <= 0 || height <= 0 || stride <= 0 || !out_buf ||
      !out_len) {
    return false;
  }
  const bool color = components_in >= 3;
  const int out_components = color ? 3 : 1;
  if (components_in != 1 && components_in != 3 && components_in != 4) {
    return false;
  }
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;

  std::vector<uint8_t> packed(static_cast<size_t>(width) * height *
                              out_components);
  for (int y = 0; y < height; ++y) {
    const uint8_t* in = src + static_cast<size_t>(y) * stride;
    uint8_t* out = packed.data() + static_cast<size_t>(y) * width *
                                       out_components;
    if (!color) {
      memcpy(out, in, static_cast<size_t>(width));
    } else if (is_bgr) {
      for (int x = 0; x < width; ++x) {
        out[x * 3 + 0] = in[x * components_in + 2];
        out[x * 3 + 1] = in[x * components_in + 1];
        out[x * 3 + 2] = in[x * components_in + 0];
      }
    } else {
      for (int x = 0; x < width; ++x) {
        out[x * 3 + 0] = in[x * components_in + 0];
        out[x * 3 + 1] = in[x * components_in + 1];
        out[x * 3 + 2] = in[x * components_in + 2];
      }
    }
  }

  jpeg_compress_struct cinfo;
  HyperJpegliErr jerr;
  memset(&cinfo, 0, sizeof(cinfo));
  cinfo.err = jpegli_std_error(&jerr.pub);
  jerr.pub.error_exit = &HyperJpegliErrorExit;

  unsigned char* mem = nullptr;
  unsigned long mem_size = 0;
  if (setjmp(jerr.setjmp_buffer)) {
    jpegli_destroy_compress(&cinfo);
    if (mem) free(mem);
    return false;
  }

  jpegli_CreateCompress(&cinfo, JPEG_LIB_VERSION,
                        sizeof(jpeg_compress_struct));
  jpegli_mem_dest(&cinfo, &mem, &mem_size);
  cinfo.image_width = static_cast<JDIMENSION>(width);
  cinfo.image_height = static_cast<JDIMENSION>(height);
  cinfo.input_components = out_components;
  cinfo.in_color_space = color ? JCS_RGB : JCS_GRAYSCALE;
  jpegli_set_defaults(&cinfo);
  jpegli_set_quality(&cinfo, quality,  TRUE);
  if (color && subsample420) {
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
  }
  if (progressive) jpegli_set_progressive_level(&cinfo, 2);

  jpegli_start_compress(&cinfo,  TRUE);
  const int row_bytes = width * out_components;
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = packed.data() +
                   static_cast<size_t>(cinfo.next_scanline) * row_bytes;
    jpegli_write_scanlines(&cinfo, &row, 1);
  }
  jpegli_finish_compress(&cinfo);
  jpegli_destroy_compress(&cinfo);

  if (!mem || mem_size == 0) {
    if (mem) free(mem);
    return false;
  }

  *out_buf = mem;
  *out_len = mem_size;
  return true;
}

__attribute__((visibility("default")))
void HyperJpegliFree(void* p) {
  if (p) free(p);
}

}
