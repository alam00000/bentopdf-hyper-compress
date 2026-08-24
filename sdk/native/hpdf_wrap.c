#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "public/fpdfview.h"
#include "public/fpdf_save.h"
#include "public/fpdf_compress.h"
#include "hpdf.h"

static int g_inited = 0;
static char g_last_error[256];

static void set_error(const char* msg) {
  snprintf(g_last_error, sizeof(g_last_error), "%s", msg ? msg : "");
}

static void ensure_init(void) {
  if (!g_inited) {
    FPDF_LIBRARY_CONFIG config;
    memset(&config, 0, sizeof(config));
    config.version = 6;
    config.m_BrotliEnabled = 1;
    FPDF_InitLibraryWithConfig(&config);
    g_inited = 1;
  }
}

typedef struct {
  FPDF_FILEWRITE fw;
  FILE* fp;
} file_writer;

static int file_write_cb(FPDF_FILEWRITE* w, const void* data,
                         unsigned long size) {
  file_writer* fw = (file_writer*)w;
  return fwrite(data, 1, size, fw->fp) == size ? 1 : 0;
}

typedef struct {
  FPDF_FILEWRITE fw;
  unsigned char* data;
  size_t size;
  size_t cap;
} mem_writer;

static int mem_write_cb(FPDF_FILEWRITE* w, const void* data,
                        unsigned long size) {
  mem_writer* mw = (mem_writer*)w;
  if (size == 0) return 1;
  if (size > SIZE_MAX - mw->size) return 0;
  size_t needed = mw->size + size;
  if (needed > mw->cap) {
    size_t ncap = mw->cap ? mw->cap : 65536;
    while (ncap < needed) {
      if (ncap > SIZE_MAX / 2) { ncap = needed; break; }
      ncap *= 2;
    }
    unsigned char* nd = (unsigned char*)realloc(mw->data, ncap);
    if (!nd) return 0;
    mw->data = nd;
    mw->cap = ncap;
  }
  memcpy(mw->data + mw->size, data, size);
  mw->size += size;
  return 1;
}

static int run_options(FPDF_DOCUMENT doc, const int* ids, const int* vals,
                       int n) {
  FPDF_COMPRESS_OPTIONS opts = HyperCompress_CreateOptions();
  for (int i = 0; i < n; ++i) {
    HyperCompress_SetOption(opts, ids[i], vals[i]);
  }
  int rc = HyperCompress_Execute(doc, opts);
  HyperCompress_CloseOptions(opts);
  return rc;
}

static int is_signed(FPDF_DOCUMENT doc, int rc) {
  return (rc == HYPERC_EXEC_SKIPPED_SIGNED) ||
         (HyperCompress_DocIsSigned(doc) != 0);
}

int hpdf_compress_file(const char* in_path, const char* out_path,
                       const int* ids, const int* vals, int n,
                       const char* password) {
  set_error("");
  ensure_init();
  FPDF_DOCUMENT doc =
      FPDF_LoadDocument(in_path, (password && *password) ? password : NULL);
  if (!doc) {
    set_error("cannot open input (missing, corrupt, or wrong password)");
    return 1;
  }

  int rc = run_options(doc, ids, vals, n);
  int signed_doc = is_signed(doc, rc);
  if (!rc && !signed_doc) {
    FPDF_CloseDocument(doc);
    set_error("engine pass failed");
    return 1;
  }
  int flags = signed_doc ? FPDF_INCREMENTAL : FPDF_NO_INCREMENTAL;

  file_writer w;
  memset(&w, 0, sizeof(w));
  w.fw.version = 1;
  w.fw.WriteBlock = file_write_cb;
  w.fp = fopen(out_path, "wb");
  if (!w.fp) {
    FPDF_CloseDocument(doc);
    set_error("cannot open output for writing");
    return 1;
  }
  int ok = FPDF_SaveAsCopy(doc, &w.fw, flags);
  fclose(w.fp);
  FPDF_CloseDocument(doc);

  if (!ok) {
    set_error("save failed");
    return 1;
  }
  return signed_doc ? 2 : 0;
}

unsigned char* hpdf_compress_buffer(const unsigned char* in_data,
                                    unsigned long in_size,
                                    const int* ids, const int* vals, int n,
                                    const char* password,
                                    unsigned long* out_size,
                                    int* out_status) {
  set_error("");
  if (out_size) *out_size = 0;
  if (out_status) *out_status = 1;
  if (in_size > (unsigned long)INT_MAX) {
    set_error("input larger than 2 GB is not supported");
    return NULL;
  }
  ensure_init();
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument(
      in_data, (int)in_size, (password && *password) ? password : NULL);
  if (!doc) {
    set_error("cannot parse input (corrupt or wrong password)");
    return NULL;
  }

  int rc = run_options(doc, ids, vals, n);
  int signed_doc = is_signed(doc, rc);
  if (!rc && !signed_doc) {
    FPDF_CloseDocument(doc);
    set_error("engine pass failed");
    return NULL;
  }
  int flags = signed_doc ? FPDF_INCREMENTAL : FPDF_NO_INCREMENTAL;

  mem_writer w;
  memset(&w, 0, sizeof(w));
  w.fw.version = 1;
  w.fw.WriteBlock = mem_write_cb;
  int ok = FPDF_SaveAsCopy(doc, &w.fw, flags);
  FPDF_CloseDocument(doc);

  if (!ok || w.size == 0) {
    free(w.data);
    set_error("save failed");
    return NULL;
  }
  if (out_size) *out_size = (unsigned long)w.size;
  if (out_status) *out_status = signed_doc ? 2 : 0;
  return w.data;
}

void hpdf_free(unsigned char* buffer) {
  free(buffer);
}

const char* hpdf_last_error(void) {
  return g_last_error;
}

const char* hpdf_version(void) {
  return HPDF_VERSION;
}
