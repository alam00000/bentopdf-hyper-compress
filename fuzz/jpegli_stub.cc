#include <cstddef>
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

bool HyperJpegliDecodeCmyk(const uint8_t* data,
                           size_t len,
                           int color_transform,
                           uint8_t** out_buf,
                           int* out_w,
                           int* out_h,
                           int* saw_adobe) {
  return false;
}

bool HyperJpegliEncodeCmyk(const uint8_t* src,
                           int width,
                           int height,
                           int quality,
                           int adobe_marker,
                           uint8_t** out_buf,
                           size_t* out_len) {
  return false;
}

bool HyperJpegliRepack(const uint8_t* data,
                       size_t len,
                       uint8_t** out_buf,
                       size_t* out_len) {
  return false;
}

}
