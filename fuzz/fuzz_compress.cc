#include <cstddef>
#include <cstdint>
#include <cstring>

#include "public/fpdfview.h"
#include "public/fpdf_save.h"
#include "public/fpdf_compress.h"

namespace {

int CountingWrite(FPDF_FILEWRITE*, const void*, unsigned long) {
  return 1;
}

void EnsureLibrary() {
  static bool inited = false;
  if (!inited) {
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 6;
    config.m_BrotliEnabled = 1;
    FPDF_InitLibraryWithConfig(&config);
    inited = true;
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 8 || size > 8 << 20) {
    return 0;
  }
  EnsureLibrary();

  FPDF_DOCUMENT doc =
      FPDF_LoadMemDocument(data, static_cast<int>(size), nullptr);
  if (!doc) {
    return 0;
  }

  FPDF_COMPRESS_OPTIONS opts = HyperCompress_CreateOptions();
  const int ids[] = {1, 2, 3, 10, 7, 6, 65, 66};
  const int vals[] = {150, 70, 4, 1, 1, 1, 1, 9};
  for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
    HyperCompress_SetOption(opts, ids[i], vals[i]);
  }
  HyperCompress_Execute(doc, opts);
  HyperCompress_CloseOptions(opts);

  FPDF_FILEWRITE fw;
  std::memset(&fw, 0, sizeof(fw));
  fw.version = 1;
  fw.WriteBlock = CountingWrite;
  FPDF_SaveAsCopy(doc, &fw, FPDF_NO_INCREMENTAL);
  FPDF_CloseDocument(doc);
  return 0;
}
