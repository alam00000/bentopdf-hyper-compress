#include <cstdint>

extern "C" {

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
  return false;
}

void HyperJpegliFree(void*) {}

}
