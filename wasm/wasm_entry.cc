#include <emscripten.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "public/fpdfview.h"
#include "public/fpdf_save.h"
#include "public/fpdf_compress.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/Pl_Flate.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/RandomDataProvider.hh>

#include <unistd.h>

namespace {

std::string& LastError() {
  static std::string last_error;
  return last_error;
}

class EntropyRandomDataProvider : public RandomDataProvider {
 public:
  void provideRandomData(unsigned char* data, size_t len) override {
    while (len > 0) {
      size_t chunk = len < 256 ? len : 256;
      if (getentropy(data, chunk) != 0) {
        throw std::runtime_error("getentropy failed");
      }
      data += chunk;
      len -= chunk;
    }
  }
};

void EnsureRandomProvider() {
  static EntropyRandomDataProvider provider;
  static bool installed = false;
  if (!installed) {
    QUtil::setRandomDataProvider(&provider);
    installed = true;
  }
}

struct MemBuf {
  unsigned char* data = nullptr;
  size_t size = 0;
  size_t cap = 0;
};

struct Writer {
  FPDF_FILEWRITE fw;
  MemBuf buf;
};

int WriterWrite(FPDF_FILEWRITE* fw, const void* data, unsigned long size) {
  Writer* w = reinterpret_cast<Writer*>(fw);
  if (size == 0) {
    return 1;
  }
  if (size > SIZE_MAX - w->buf.size) {
    return 0;
  }
  size_t needed = w->buf.size + size;
  if (needed > w->buf.cap) {
    size_t ncap = w->buf.cap ? w->buf.cap : 65536;
    while (ncap < needed) {
      if (ncap > SIZE_MAX / 2) {
        ncap = needed;
        break;
      }
      ncap *= 2;
    }
    unsigned char* nd =
        static_cast<unsigned char*>(realloc(w->buf.data, ncap));
    if (!nd) {
      return 0;
    }
    w->buf.data = nd;
    w->buf.cap = ncap;
  }
  memcpy(w->buf.data + w->buf.size, data, size);
  w->buf.size += size;
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

unsigned char* CopyOut(const unsigned char* src, size_t len,
                       unsigned long* out_size) {
  if (!src || len == 0) {
    return nullptr;
  }
  unsigned char* out = static_cast<unsigned char*>(malloc(len));
  if (!out) {
    return nullptr;
  }
  memcpy(out, src, len);
  *out_size = static_cast<unsigned long>(len);
  return out;
}

template <typename Mutate, typename Configure>
unsigned char* QpdfRewrite(const unsigned char* in, unsigned long in_size,
                           const char* password, unsigned long* out_size,
                           Mutate mutate, Configure configure) {
  try {
    LastError().clear();
    EnsureRandomProvider();
    std::shared_ptr<QPDF> pdf = QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->processMemoryFile("in.pdf", reinterpret_cast<const char*>(in),
                           static_cast<size_t>(in_size),
                           (password && *password) ? password : nullptr);

    mutate(*pdf);

    QPDFWriter w(*pdf);
    w.setOutputMemory();
    configure(w);
    w.write();

    std::shared_ptr<Buffer> buf = w.getBufferSharedPointer();
    if (!buf) {
      return nullptr;
    }
    return CopyOut(buf->getBuffer(), buf->getSize(), out_size);
  } catch (const std::exception& e) {
    LastError() = e.what();
    return nullptr;
  }
}

const auto kNoMutation = [](QPDF&) {};

}

extern "C" {

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_compress_buffer(const unsigned char* in,
                                     unsigned long in_size,
                                     const char* password,
                                     const int* opt_ids, const int* opt_vals,
                                     int opt_count, unsigned long* out_size,
                                     int* out_signed) {
  *out_size = 0;
  if (out_signed) {
    *out_signed = 0;
  }
  EnsureLibrary();

  if (in_size > static_cast<unsigned long>(INT_MAX)) {
    LastError() = "input larger than 2 GB is not supported";
    return nullptr;
  }
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument(
      in, static_cast<int>(in_size), (password && *password) ? password : nullptr);
  if (!doc) {
    LastError() = "cannot parse input (corrupt or wrong password)";
    return nullptr;
  }

  FPDF_COMPRESS_OPTIONS opts = HyperCompress_CreateOptions();
  for (int i = 0; i < opt_count; ++i) {
    HyperCompress_SetOption(opts, opt_ids[i], opt_vals[i]);
  }

  int rc = HyperCompress_Execute(doc, opts);
  bool signed_doc = (rc == HYPERC_EXEC_SKIPPED_SIGNED) ||
                    (HyperCompress_DocIsSigned(doc) != 0);
  if (!rc && !signed_doc) {
    HyperCompress_CloseOptions(opts);
    FPDF_CloseDocument(doc);
    LastError() = "engine pass failed";
    return nullptr;
  }
  int save_flags = signed_doc ? FPDF_INCREMENTAL : FPDF_NO_INCREMENTAL;

  Writer w;
  memset(&w, 0, sizeof(w));
  w.fw.version = 1;
  w.fw.WriteBlock = WriterWrite;
  int saved = FPDF_SaveAsCopy(doc, &w.fw, save_flags);

  HyperCompress_CloseOptions(opts);
  FPDF_CloseDocument(doc);

  if (!saved || w.buf.size == 0) {
    free(w.buf.data);
    return nullptr;
  }
  if (out_signed) {
    *out_signed = signed_doc ? 1 : 0;
  }
  *out_size = static_cast<unsigned long>(w.buf.size);
  return w.buf.data;
}

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_decrypt_buffer(const unsigned char* in,
                                    unsigned long in_size,
                                    const char* password,
                                    unsigned long* out_size) {
  *out_size = 0;
  return QpdfRewrite(in, in_size, password, out_size, kNoMutation,
                     [](QPDFWriter& w) { w.setPreserveEncryption(false); });
}

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_pack_buffer(const unsigned char* in,
                                 unsigned long in_size,
                                 unsigned long* out_size) {
  *out_size = 0;
  return QpdfRewrite(in, in_size, nullptr, out_size, kNoMutation,
                     [](QPDFWriter& w) {
                       w.setObjectStreamMode(qpdf_o_generate);
                       w.setCompressStreams(true);
                       w.setRecompressFlate(true);
                       Pl_Flate::setCompressionLevel(9);
                     });
}

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_pack_buffer_pdfa(const unsigned char* in,
                                      unsigned long in_size,
                                      unsigned long* out_size) {
  *out_size = 0;
  return QpdfRewrite(in, in_size, nullptr, out_size, kNoMutation,
                     [](QPDFWriter& w) {
                       w.setObjectStreamMode(qpdf_o_disable);
                       w.setCompressStreams(true);
                       w.setRecompressFlate(true);
                       w.forcePDFVersion("1.4");
                       Pl_Flate::setCompressionLevel(9);
                     });
}

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_get_xmp(const unsigned char* in, unsigned long in_size,
                             const char* password, unsigned long* out_size) {
  *out_size = 0;
  try {
    LastError().clear();
    EnsureRandomProvider();
    std::shared_ptr<QPDF> pdf = QPDF::create();
    pdf->setSuppressWarnings(true);
    pdf->processMemoryFile("in.pdf", reinterpret_cast<const char*>(in),
                           static_cast<size_t>(in_size),
                           (password && *password) ? password : nullptr);

    QPDFObjectHandle meta = pdf->getRoot().getKey("/Metadata");
    if (!meta.isStream()) {
      return nullptr;
    }
    std::shared_ptr<Buffer> buf = meta.getStreamData(qpdf_dl_all);
    if (!buf || buf->getSize() == 0) {
      return nullptr;
    }
    unsigned char* out = static_cast<unsigned char*>(malloc(buf->getSize() + 1));
    if (!out) {
      return nullptr;
    }
    memcpy(out, buf->getBuffer(), buf->getSize());
    out[buf->getSize()] = 0;
    *out_size = static_cast<unsigned long>(buf->getSize());
    return out;
  } catch (const std::exception& e) {
    LastError() = e.what();
    return nullptr;
  }
}

EMSCRIPTEN_KEEPALIVE
unsigned char* hyper_set_xmp(const unsigned char* in, unsigned long in_size,
                             const char* xmp, unsigned long xmp_size,
                             unsigned long* out_size) {
  *out_size = 0;
  return QpdfRewrite(in, in_size, nullptr, out_size,
                     [&](QPDF& pdf) {
                       QPDFObjectHandle stream = QPDFObjectHandle::newStream(
                           &pdf, std::string(xmp, xmp_size));
                       stream.getDict().replaceKey(
                           "/Type", QPDFObjectHandle::newName("/Metadata"));
                       stream.getDict().replaceKey(
                           "/Subtype", QPDFObjectHandle::newName("/XML"));
                       pdf.getRoot().replaceKey("/Metadata", stream);
                     },
                     [](QPDFWriter& w) {
                       w.setStreamDataMode(qpdf_s_preserve);
                     });
}

EMSCRIPTEN_KEEPALIVE
const char* hyper_last_error() {
  return LastError().c_str();
}

EMSCRIPTEN_KEEPALIVE
void hyper_free(unsigned char* p) {
  free(p);
}

}
