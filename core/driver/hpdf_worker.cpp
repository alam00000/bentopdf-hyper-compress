#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "public/fpdfview.h"
#include "public/fpdf_save.h"
#include "public/fpdf_compress.h"

static FILE* g_out = nullptr;
static int WriteCB(FPDF_FILEWRITE* w, const void* data, unsigned long size) {
  (void)w;
  return fwrite(data, 1, size, g_out) == size ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s in.pdf out.pdf [opt=val ...]\n", argv[0]); return 2; }
  FPDF_LIBRARY_CONFIG config = {};
  config.version = 6;
  config.m_BrotliEnabled = 1;
  FPDF_InitLibraryWithConfig(&config);
  FPDF_DOCUMENT doc = FPDF_LoadDocument(argv[1], nullptr);
  if (!doc) { fprintf(stderr, "load failed err=%lu\n", FPDF_GetLastError()); return 3; }

  FPDF_COMPRESS_OPTIONS opts = HyperCompress_CreateOptions();
  for (int i = 3; i < argc; ++i) {
    int o = 0, v = 0;
    if (sscanf(argv[i], "%d=%d", &o, &v) == 2) {
      HyperCompress_SetOption(opts, o, v);
    }
  }
  int rc = HyperCompress_Execute(doc, opts);

  bool signed_doc =
      (rc == HYPERC_EXEC_SKIPPED_SIGNED) || (HyperCompress_DocIsSigned(doc) != 0);
  int save_flags = signed_doc ? FPDF_INCREMENTAL : FPDF_NO_INCREMENTAL;
  if (signed_doc)
    fprintf(stderr, "SIGNED_SKIP: signed document, saving INCREMENTAL\n");

  g_out = fopen(argv[2], "wb");
  if (!g_out) { fprintf(stderr, "cannot open out\n"); return 4; }
  FPDF_FILEWRITE fw; memset(&fw, 0, sizeof(fw));
  fw.version = 1; fw.WriteBlock = WriteCB;
  FPDF_SaveAsCopy(doc, &fw, save_flags);
  fclose(g_out);

  HyperCompress_CloseOptions(opts);
  FPDF_CloseDocument(doc);

  fflush(stderr); fflush(stdout);
  _Exit(rc ? 0 : 1);
}
