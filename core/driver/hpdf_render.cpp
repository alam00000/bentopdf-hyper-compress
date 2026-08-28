#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "public/fpdfview.h"

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s in.pdf out.ppm [scale]\n", argv[0]); return 2; }
  double scale = argc > 3 ? atof(argv[3]) : 2.0;
  FPDF_LIBRARY_CONFIG config = {};
  config.version = 6;
  config.m_BrotliEnabled = 1;
  FPDF_InitLibraryWithConfig(&config);
  FPDF_DOCUMENT doc = FPDF_LoadDocument(argv[1], nullptr);
  if (!doc) { fprintf(stderr, "load failed err=%lu\n", FPDF_GetLastError()); return 3; }
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  if (!page) { fprintf(stderr, "page load failed\n"); return 4; }
  double raw_w = FPDF_GetPageWidth(page) * scale;
  double raw_h = FPDF_GetPageHeight(page) * scale;
  if (!(raw_w >= 1.0) || !(raw_h >= 1.0) ||
      raw_w * raw_h > 256.0 * 1024.0 * 1024.0) {
    fprintf(stderr, "page too large: %gx%g\n", raw_w, raw_h);
    FPDF_ClosePage(page); FPDF_CloseDocument(doc); return 5;
  }
  int w = (int)raw_w;
  int h = (int)raw_h;
  FPDF_BITMAP bmp = FPDFBitmap_Create(w, h, 0);
  if (!bmp) {
    fprintf(stderr, "bitmap alloc failed for %dx%d\n", w, h);
    FPDF_ClosePage(page); FPDF_CloseDocument(doc); return 5;
  }
  FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
  FPDF_RenderPageBitmap(bmp, page, 0, 0, w, h, 0, FPDF_ANNOT);
  const uint8_t* buf = (const uint8_t*)FPDFBitmap_GetBuffer(bmp);
  int stride = FPDFBitmap_GetStride(bmp);
  FILE* f = fopen(argv[2], "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = buf + (size_t)y * stride;
    for (int x = 0; x < w; ++x) {

      uint8_t b = row[x*4+0], g = row[x*4+1], r = row[x*4+2];
      fputc(r, f); fputc(g, f); fputc(b, f);
    }
  }
  fclose(f);
  fprintf(stderr, "rendered %dx%d\n", w, h);
  FPDFBitmap_Destroy(bmp);
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  fflush(stderr);
  _Exit(0);
}
