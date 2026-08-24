#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>

#include "third_party/afdko/shared/include/ctlshare.h"
#include "third_party/afdko/shared/include/t1read.h"
#include "third_party/afdko/shared/include/cffwrite.h"
#include "third_party/afdko/shared/include/absfont.h"

namespace {

struct Stream {
  const uint8_t* src_data = nullptr;
  size_t src_len = 0;
  std::vector<uint8_t> buf;
  size_t pos = 0;
};

struct ConvCtx {
  Stream t1r_src;
  Stream t1r_tmp;
  Stream cfw_dst;
  Stream cfw_tmp;
};

void* StreamOpen(ctlStreamCallbacks* cb, int id, size_t  ) {
  ConvCtx* ctx = static_cast<ConvCtx*>(cb->direct_ctx);
  if (!ctx) return nullptr;
  Stream* s = nullptr;
  switch (id) {
    case T1R_SRC_STREAM_ID: s = &ctx->t1r_src; break;
    case T1R_TMP_STREAM_ID: s = &ctx->t1r_tmp; break;
    case CFW_DST_STREAM_ID: s = &ctx->cfw_dst; break;
    case CFW_TMP_STREAM_ID: s = &ctx->cfw_tmp; break;
    default:

      return nullptr;
  }
  s->pos = 0;
  return s;
}

int StreamSeek(ctlStreamCallbacks*  , void* stream, long offset) {
  Stream* s = static_cast<Stream*>(stream);
  if (!s || offset < 0) return 1;
  size_t off = static_cast<size_t>(offset);
  if (s->src_data) {
    if (off > s->src_len) return 1;
    s->pos = off;
    return 0;
  }
  if (off > s->buf.size()) s->buf.resize(off);
  s->pos = off;
  return 0;
}

long StreamTell(ctlStreamCallbacks*  , void* stream) {
  Stream* s = static_cast<Stream*>(stream);
  return s ? static_cast<long>(s->pos) : -1;
}

size_t StreamRead(ctlStreamCallbacks*  , void* stream, char** ptr) {
  Stream* s = static_cast<Stream*>(stream);
  if (!s) { *ptr = nullptr; return 0; }
  const uint8_t* data = s->src_data ? s->src_data : s->buf.data();
  size_t len = s->src_data ? s->src_len : s->buf.size();
  if (s->pos >= len) { *ptr = nullptr; return 0; }
  size_t remaining = len - s->pos;
  *ptr = const_cast<char*>(reinterpret_cast<const char*>(data + s->pos));
  s->pos = len;
  return remaining;
}

size_t StreamWrite(ctlStreamCallbacks*  , void* stream,
                    size_t count, const char* ptr) {
  Stream* s = static_cast<Stream*>(stream);
  if (!s || s->src_data || !ptr) return 0;
  if (s->pos + count > s->buf.size()) s->buf.resize(s->pos + count);
  memcpy(s->buf.data() + s->pos, ptr, count);
  s->pos += count;
  return count;
}

int StreamStatus(ctlStreamCallbacks*  , void*  ) { return 0; }
int StreamClose(ctlStreamCallbacks*  , void*  ) { return 0; }

void* MemoryManage(ctlMemoryCallbacks*  , void* old, size_t size) {
  if (size == 0) {
    if (old) free(old);
    return nullptr;
  }
  return realloc(old, size);
}

}

extern "C" {

__attribute__((visibility("default")))
bool HyperType1ToCff(const uint8_t* in_buf,
                     size_t in_len,
                     uint8_t** out_buf,
                     size_t* out_len) {
  if (!in_buf || !out_buf || !out_len || in_len == 0) return false;

  ConvCtx ctx;
  ctx.t1r_src.src_data = in_buf;
  ctx.t1r_src.src_len = in_len;

  ctlMemoryCallbacks mem_cb = {};
  mem_cb.manage = &MemoryManage;

  ctlStreamCallbacks t1r_stm = {};
  t1r_stm.direct_ctx = &ctx;
  t1r_stm.open = &StreamOpen;
  t1r_stm.seek = &StreamSeek;
  t1r_stm.tell = &StreamTell;
  t1r_stm.read = &StreamRead;
  t1r_stm.write = &StreamWrite;
  t1r_stm.status = &StreamStatus;
  t1r_stm.close = &StreamClose;

  ctlStreamCallbacks cfw_stm = t1r_stm;

  t1rCtx t1r =
      t1rNew(&mem_cb, &t1r_stm, T1R_CHECK_ARGS);
  if (!t1r) return false;
  cfwCtx cfw =
      cfwNew(&mem_cb, &cfw_stm, CFW_CHECK_ARGS);
  if (!cfw) { t1rFree(t1r); return false; }

  abfTopDict* top = nullptr;
  if (t1rBegFont(t1r, 0,  0, &top,  nullptr) || !top) {
    cfwFree(cfw); t1rFree(t1r);
    return false;
  }

  if (cfwBegSet(cfw,  0) ||
      cfwBegFont(cfw,  nullptr,  65535,
                   nullptr)) {
    t1rEndFont(t1r);
    cfwFree(cfw); t1rFree(t1r);
    return false;
  }

  abfGlyphCallbacks glyph_cb = cfwGlyphCallbacks;
  glyph_cb.direct_ctx = cfw;

  if (t1rIterateGlyphs(t1r, &glyph_cb)) {
    cfwEndFont(cfw, top);
    cfwEndSet(cfw);
    t1rEndFont(t1r);
    cfwFree(cfw); t1rFree(t1r);
    return false;
  }

  t1rEndFont(t1r);
  if (cfwEndFont(cfw, top) || cfwEndSet(cfw)) {
    cfwFree(cfw); t1rFree(t1r);
    return false;
  }

  cfwFree(cfw);
  t1rFree(t1r);

  std::vector<uint8_t>& dst_storage = ctx.cfw_dst.buf;
  if (dst_storage.empty()) return false;

  uint8_t* buf = static_cast<uint8_t*>(malloc(dst_storage.size()));
  if (!buf) return false;
  memcpy(buf, dst_storage.data(), dst_storage.size());
  *out_buf = buf;
  *out_len = dst_storage.size();
  return true;
}

__attribute__((visibility("default")))
void HyperType1Free(void* buf) {
  if (buf) free(buf);
}

}
