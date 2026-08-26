#include "public/fpdf_compress.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/fpdfapi/edit/cpdf_pagecontentgenerator.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_clippath.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fxcodec/jbig2/jbig2_decoder.h"
#include "core/fxcodec/jbig2/JBig2_DocumentContext.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pageobjectholder.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/font/cpdf_fontencoding.h"
#include "core/fxge/cfx_face.h"
#include "core/fxge/cfx_font.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_boolean.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fxcodec/fax/faxmodule.h"
#include "core/fxcodec/flate/flatemodule.h"
#include "third_party/brotli/include/brotli/encode.h"
#include "core/fxcodec/jpeg/jpegmodule.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/cfx_read_only_container_stream.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_memory.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxge/dib/cfx_dibbase.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/dib/fx_dib.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/fpdf_flatten.h"
#include "public/fpdf_formfill.h"
#include "public/fpdfview.h"

#include "third_party/lcms/include/lcms2.h"

#include "hyper_generic_cmyk_icc.h"

#include "third_party/libopenjpeg/openjpeg.h"

#include "third_party/harfbuzz-ng/src/src/hb-subset.h"
#include "third_party/harfbuzz-ng/src/src/hb.h"

static bool HyperIsLoadablePageDict(const CPDF_Dictionary* page_dict) {
  if (!page_dict)
    return false;
  RetainPtr<const CPDF_Object> type = page_dict->GetObjectFor("Type");
  if (!type)
    return true;
  const CPDF_Name* name = ToName(type->GetDirect().Get());
  return name && name->GetString() == "Page";
}

namespace {

struct CompressOptions {

  int image_max_dpi = 0;
  int image_threshold_dpi = 0;
  int image_quality = 75;
  int image_encoding = HYPERC_IMAGE_ENCODING_AUTO;
  int image_grayscale = 0;
  int image_color_target = HYPERC_COLOR_TARGET_ORIGINAL;
  int image_resample_quality = 1;

  int jpeg_subsample = 0;

  int jpeg_optimized_huffman = 0;
  int jpeg_progressive = 0;
  int clip_images = 0;
  int reduce_color_complexity = 0;
  int image_index_min_dpi = 150;

  int image_color_max_dpi = 0;
  int image_gray_max_dpi = 0;
  int image_mono_max_dpi = 0;
  int image_lossy_index = 0;

  int image_prefer_jpx = 0;

  int font_subset = 1;
  int font_remove_standard = 0;
  int unembed_aliased_fonts = 0;

  int font_merge = 0;
  int font_dedup_dicts = 0;

  int dedup_objects = 0;
  int optimize_resources = 0;

  int flatten_icc = 0;

  int convert_to_bitmap = 0;
  int convert_to_bitmap_dpi = 150;
  int convert_to_bitmap_quality = 50;

  uint32_t discard_mask = 0;

  int mrc_mode = HYPERC_MRC_OFF;
  int mrc_selector_dpi = 300;
  int mrc_bg_dpi = 75;
  int mrc_bg_quality = 30;
  int mrc_fg_quality = 50;

  int recompress_content_streams = 1;
  int stream_codec = 0;
  int brotli_quality = 11;
  int pdfa_mode = 0;
};

CompressOptions* GetOpts(FPDF_COMPRESS_OPTIONS h) {
  return reinterpret_cast<CompressOptions*>(h);
}

ByteString StripSubsetPrefix(const ByteString& name) {
  if (name.GetLength() >= 7 && name[6] == '+') {
    bool all_upper = true;
    for (size_t i = 0; i < 6; ++i) {
      if (name[i] < 'A' || name[i] > 'Z') {
        all_upper = false;
        break;
      }
    }
    if (all_upper) {
      return name.Last(name.GetLength() - 7);
    }
  }
  return name;
}

ByteString MakeSubsetTag(pdfium::span<const uint8_t> subset_bytes) {
  uint32_t h = 0x811c9dc5u;
  for (uint8_t b : subset_bytes) {
    h = (h ^ b) * 0x01000193u;
  }
  char tag[8];
  for (int i = 0; i < 6; ++i) {
    tag[i] = static_cast<char>('A' + ((h >> (i * 5)) & 0x1Fu) % 26);
  }
  tag[6] = '+';
  tag[7] = '\0';
  return ByteString(tag);
}

bool IsStandard14FontName(const ByteString& name) {
  const ByteString base = StripSubsetPrefix(name);

  static const char* const kStd14[] = {
      "Times-Roman",      "Times-Bold",          "Times-Italic",
      "Times-BoldItalic", "Helvetica",           "Helvetica-Bold",
      "Helvetica-Oblique", "Helvetica-BoldOblique",
      "Courier",          "Courier-Bold",        "Courier-Oblique",
      "Courier-BoldOblique", "Symbol",           "ZapfDingbats",
  };
  for (const char* canonical : kStd14) {
    if (base == canonical) {
      return true;
    }
  }
  return false;
}

enum class Std14Class { kNone, kLatin, kSymbol, kZapf };

Std14Class ClassifyStandard14(const ByteString& stripped) {
  if (stripped == "Symbol") return Std14Class::kSymbol;
  if (stripped == "ZapfDingbats") return Std14Class::kZapf;
  if (IsStandard14FontName(stripped)) return Std14Class::kLatin;
  return Std14Class::kNone;
}

ByteString MapAliasToStandard14(const ByteString& stripped,
                                Std14Class* out_cls) {
  *out_cls = Std14Class::kNone;
  std::string s(stripped.c_str(), stripped.GetLength());
  for (char& c : s)
    c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;

  char fam = 0;
  {
    static constexpr struct { const char* tok; char fam; } kFamilies[] = {
        {"timesnewroman", 'T'}, {"liberationserif", 'T'},
        {"nimbusromanno9l", 'T'}, {"nimbusromno9l", 'T'}, {"nimbusroman", 'T'},
        {"couriernew", 'C'}, {"liberationmono", 'C'},
        {"nimbusmonops", 'C'}, {"nimbusmonl", 'C'}, {"nimbusmono", 'C'},
        {"arial", 'H'}, {"helvetica", 'H'}, {"liberationsans", 'H'},
        {"nimbussansl", 'H'}, {"nimbussanl", 'H'}, {"nimbussans", 'H'},
    };
    std::string rest;
    for (const auto& f : kFamilies) {
      const size_t n = std::strlen(f.tok);
      if (s.size() >= n && s.compare(0, n, f.tok) == 0) {
        fam = f.fam;
        rest = s.substr(n);
        break;
      }
    }
    if (!fam)
      return ByteString();

    static constexpr const char* kSuffixTokens[] = {
        "psmt", "ps", "mt", "bolditalic", "boldoblique", "bold", "italic",
        "oblique", "regular", "regu", "roman",
    };
    bool bold = false, ital = false;
    while (!rest.empty()) {
      if (rest[0] == '-' || rest[0] == ',' || rest[0] == '_' ||
          rest[0] == ' ' || rest[0] == '.') {
        rest.erase(0, 1);
        continue;
      }
      bool matched = false;
      for (const char* tok : kSuffixTokens) {
        const size_t n = std::strlen(tok);
        if (rest.size() >= n && rest.compare(0, n, tok) == 0) {
          if (std::strstr(tok, "bold")) bold = true;
          if (std::strstr(tok, "italic") || std::strstr(tok, "oblique"))
            ital = true;
          rest.erase(0, n);
          matched = true;
          break;
        }
      }
      if (!matched)
        return ByteString();
    }
    *out_cls = Std14Class::kLatin;
    if (fam == 'T')
      return bold && ital ? "Times-BoldItalic"
             : bold       ? "Times-Bold"
             : ital       ? "Times-Italic"
                          : "Times-Roman";
    if (fam == 'C')
      return bold && ital ? "Courier-BoldOblique"
             : bold       ? "Courier-Bold"
             : ital       ? "Courier-Oblique"
                          : "Courier";
    return bold && ital ? "Helvetica-BoldOblique"
           : bold       ? "Helvetica-Bold"
           : ital       ? "Helvetica-Oblique"
                        : "Helvetica";
  }
}

FontEncoding EncodingFromName(const ByteString& name) {
  if (name == "WinAnsiEncoding") return FontEncoding::kWinAnsi;
  if (name == "MacRomanEncoding") return FontEncoding::kMacRoman;
  if (name == "StandardEncoding") return FontEncoding::kStandard;
  if (name == "PDFDocEncoding") return FontEncoding::kPdfDoc;
  if (name == "MacExpertEncoding") return FontEncoding::kMacExpert;
  return FontEncoding::kBuiltin;
}

std::unordered_set<ByteString> BuiltinGlyphNames(Std14Class cls) {
  std::unordered_set<ByteString> names;
  auto add = [&](FontEncoding enc) {
    for (int c = 0; c < 256; ++c) {
      const char* n = CharNameFromPredefinedCharSet(
          enc, static_cast<uint8_t>(c));
      if (n && *n && strcmp(n, ".notdef") != 0) {
        names.insert(ByteString(n));
      }
    }
  };
  switch (cls) {
    case Std14Class::kSymbol:
      add(FontEncoding::kAdobeSymbol);
      break;
    case Std14Class::kZapf:
      add(FontEncoding::kZapfDingbats);
      break;
    case Std14Class::kLatin:

      add(FontEncoding::kWinAnsi);
      add(FontEncoding::kStandard);
      add(FontEncoding::kMacRoman);
      break;
    default:
      break;
  }
  return names;
}

bool SafeToUnembedStandardFont(const CPDF_Dictionary* font_dict,
                               Std14Class cls,
                               bool is_subset) {
  if (cls == Std14Class::kNone) return false;
  RetainPtr<const CPDF_Object> enc = font_dict->GetDirectObjectFor("Encoding");
  if (!enc) {

    return !is_subset;
  }

  const std::unordered_set<ByteString> valid = BuiltinGlyphNames(cls);

  FontEncoding base = (cls == Std14Class::kSymbol) ? FontEncoding::kAdobeSymbol
                      : (cls == Std14Class::kZapf) ? FontEncoding::kZapfDingbats
                                                   : FontEncoding::kStandard;
  std::map<uint32_t, ByteString> diffs;
  if (enc->IsName()) {
    FontEncoding e = EncodingFromName(enc->GetString());
    if (e == FontEncoding::kBuiltin) return false;
    base = e;
  } else if (const CPDF_Dictionary* ed = enc->AsDictionary()) {
    ByteString be = ed->GetNameFor("BaseEncoding");
    if (!be.IsEmpty()) {
      FontEncoding e = EncodingFromName(be);
      if (e == FontEncoding::kBuiltin) return false;
      base = e;
    }
    RetainPtr<const CPDF_Array> da = ed->GetArrayFor("Differences");
    if (da) {
      uint32_t code = 0;
      for (size_t i = 0; i < da->size(); ++i) {
        RetainPtr<const CPDF_Object> e = da->GetDirectObjectAt(i);
        if (!e) continue;
        if (e->IsNumber()) {
          code = static_cast<uint32_t>(e->GetInteger());
        } else if (e->IsName()) {
          diffs[code++] = e->GetString();
        }
      }
    }
  } else {
    return false;
  }

  int first = font_dict->GetIntegerFor("FirstChar", 0);
  int last = font_dict->GetIntegerFor("LastChar", 255);
  if (first < 0) first = 0;
  if (last > 255) last = 255;
  for (int c = first; c <= last; ++c) {
    ByteString gname;
    auto it = diffs.find(static_cast<uint32_t>(c));
    if (it != diffs.end()) {
      gname = it->second;
    } else {
      const char* n =
          CharNameFromPredefinedCharSet(base, static_cast<uint8_t>(c));
      if (n) gname = ByteString(n);
    }
    if (gname.IsEmpty() || gname == ".notdef") {
      continue;
    }
    if (!valid.count(gname)) {
      return false;
    }
  }
  return true;
}

struct StreamDigest {
  uint64_t hi = 0;
  uint64_t lo = 0;
  bool operator==(const StreamDigest& o) const {
    return hi == o.hi && lo == o.lo;
  }
};
struct StreamDigestHash {
  size_t operator()(const StreamDigest& d) const {
    return static_cast<size_t>(d.hi ^ d.lo);
  }
};

StreamDigest DigestStream(const CPDF_Stream* stream) {
  StreamDigest d;
  if (!stream) {
    return d;
  }
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(stream));
  acc->LoadAllDataRaw();
  pdfium::span<const uint8_t> span = acc->GetSpan();
  uint64_t h1 = 1469598103934665603ULL;
  uint64_t h2 = 0xcbf29ce484222325ULL;
  for (uint8_t b : span) {
    h1 ^= b;
    h1 *= 1099511628211ULL;
    h2 = (h2 << 5) - h2 + b;
  }
  d.hi = h1 ^ (static_cast<uint64_t>(span.size()) << 32);
  d.lo = h2;
  return d;
}

uint64_t DigestAnyObject(const CPDF_Object* obj);

uint64_t DigestDictForDedup(const CPDF_Dictionary* dict) {
  if (!dict) return 0;
  std::vector<std::pair<ByteString, uint64_t>> entries;
  entries.reserve(dict->size());
  CPDF_DictionaryLocker locker(dict);
  for (const auto& it : locker) {
    const ByteString& k = it.first;

    if (k == "Length" || k == "Name")
      continue;
    const CPDF_Object* v = it.second.Get();
    uint64_t vh = 0;
    if (v) {
      if (v->IsReference()) {
        vh = (uint64_t)v->AsReference()->GetRefObjNum() | 0x100000000ULL;
      } else if (v->IsString()) {
        ByteString s = v->GetString();
        vh = 1;
        for (size_t i = 0; i < s.GetLength(); ++i) {
          vh = vh * 1099511628211ULL ^ (uint8_t)s[i];
        }
        vh |= 0x200000000ULL;
      } else if (v->IsName()) {
        ByteString s = v->GetString();
        vh = 1;
        for (size_t i = 0; i < s.GetLength(); ++i) {
          vh = vh * 1099511628211ULL ^ (uint8_t)s[i];
        }
        vh |= 0x300000000ULL;
      } else if (v->IsNumber()) {
        double n = v->AsNumber()->GetNumber();
        std::memcpy(&vh, &n, sizeof(double));
        vh |= 0x400000000ULL;
      } else if (v->IsBoolean()) {
        vh = v->GetInteger() ? 0x500000001ULL : 0x500000000ULL;
      } else if (v->IsDictionary()) {
        vh = DigestDictForDedup(v->AsDictionary());
      } else if (v->IsArray()) {
        const CPDF_Array* a = v->AsArray();
        vh = 0x700000000ULL ^ a->size();
        for (size_t i = 0; i < a->size(); ++i) {
          RetainPtr<const CPDF_Object> ai = a->GetObjectAt(i);
          vh = vh * 1099511628211ULL ^ DigestAnyObject(ai.Get());
        }
      } else if (v->IsStream()) {

        vh = 0x800000000ULL ^ DigestAnyObject(v);
      }
    }
    entries.emplace_back(k, vh);
  }
  std::sort(entries.begin(), entries.end(),
            [](const std::pair<ByteString, uint64_t>& a,
               const std::pair<ByteString, uint64_t>& b) {
              return a.first < b.first;
            });
  uint64_t h = 1469598103934665603ULL;
  for (const auto& e : entries) {
    for (size_t i = 0; i < e.first.GetLength(); ++i) {
      h ^= (uint8_t)e.first[i];
      h *= 1099511628211ULL;
    }
    h ^= e.second;
    h *= 1099511628211ULL;
  }
  return h;
}

uint64_t DigestAnyObject(const CPDF_Object* obj) {
  if (!obj) return 0;
  if (obj->IsDictionary()) {
    return DigestDictForDedup(obj->AsDictionary());
  }
  if (obj->IsStream()) {

    const CPDF_Stream* s = obj->AsStream();
    uint64_t dh = DigestDictForDedup(s->GetDict().Get());
    StreamDigest sd = DigestStream(s);
    return (dh * 1099511628211ULL) ^ sd.lo ^ (sd.hi * 0x9E3779B97F4A7C15ULL);
  }
  if (obj->IsArray()) {
    const CPDF_Array* a = obj->AsArray();
    uint64_t h = 0x700000000ULL ^ static_cast<uint64_t>(a->size());
    for (size_t i = 0; i < a->size(); ++i) {
      RetainPtr<const CPDF_Object> ai = a->GetObjectAt(i);
      uint64_t ih = 0;
      if (ai) {
        if (ai->IsReference()) {
          ih = static_cast<uint64_t>(ai->AsReference()->GetRefObjNum())
               | 0x100000000ULL;
        } else if (ai->IsName() || ai->IsString()) {
          ByteString s = ai->GetString();
          uint64_t sh = 1;
          for (size_t k = 0; k < s.GetLength(); ++k) {
            sh = sh * 1099511628211ULL ^ static_cast<uint8_t>(s[k]);
          }
          ih = sh | (ai->IsName() ? 0x300000000ULL : 0x200000000ULL);
        } else if (ai->IsNumber()) {
          double n = ai->AsNumber()->GetNumber();
          uint64_t nh = 0;
          std::memcpy(&nh, &n, sizeof(double));
          ih = nh | 0x400000000ULL;
        } else if (ai->IsBoolean()) {
          ih = ai->GetInteger() ? 0x500000001ULL : 0x500000000ULL;
        } else if (ai->IsDictionary() || ai->IsArray() || ai->IsStream()) {
          ih = DigestAnyObject(ai.Get());
        }
      }
      h = (h * 1099511628211ULL) ^ ih;
    }
    return h;
  }
  if (obj->IsString() || obj->IsName()) {
    ByteString s = obj->GetString();
    uint64_t h = 1;
    for (size_t i = 0; i < s.GetLength(); ++i) {
      h = h * 1099511628211ULL ^ static_cast<uint8_t>(s[i]);
    }
    return h | (obj->IsName() ? 0x300000000ULL : 0x200000000ULL);
  }
  if (obj->IsNumber()) {
    double n = obj->AsNumber()->GetNumber();
    uint64_t h = 0;
    std::memcpy(&h, &n, sizeof(double));
    return h | 0x400000000ULL;
  }
  if (obj->IsBoolean()) {
    return obj->GetInteger() ? 0x500000001ULL : 0x500000000ULL;
  }
  if (obj->IsReference()) {
    return static_cast<uint64_t>(obj->AsReference()->GetRefObjNum())
           | 0x100000000ULL;
  }
  return 0;
}

extern "C" {
bool HyperType1ToCff(const uint8_t* in_buf,
                     size_t in_len,
                     uint8_t** out_buf,
                     size_t* out_len);
void HyperType1Free(void* buf);

void* HyperJbig2BeginDoc(void);
bool HyperJbig2AddPage(void* ctx, const uint8_t* src_data,
                       int src_stride, int width, int height);
bool HyperJbig2FinishDoc(void* ctx, uint8_t** out_globals,
                         size_t* out_globals_len);
bool HyperJbig2GetPage(void* ctx, int page_index,
                       uint8_t** out_buf, size_t* out_len);
void HyperJbig2EndDoc(void* ctx);
void HyperJbig2Free(void* buf);

bool HyperQuantizeRgbToIndexed(const uint8_t* bgra, int width, int height,
                               int src_stride, int bpp, int max_colors,
                               uint8_t** out_indices, uint8_t** out_palette,
                               int* out_ncolors, double* out_mean_err);

bool HyperJpegliEncode(const uint8_t* src, int width, int height, int stride,
                       int comp, int is_bgr, int quality, int progressive,
                       int subsample420, uint8_t** out_buf, size_t* out_len);
void HyperJpegliFree(void* buf);
}

class HyperFontOptimize {
 public:
  HyperFontOptimize(CPDF_Document* doc, const CompressOptions& opts)
      : doc_(doc), opts_(opts) {}

  int Run() {
    int changes = 0;
    if (opts_.font_remove_standard || opts_.unembed_aliased_fonts) {
      changes += RemoveStandardFontPrograms();
    }

    if (opts_.font_subset) {
      changes += ConvertType1FontsToCff();
    }
    if (opts_.font_merge) {
      changes += MergeFontPrograms();
    }

    if (opts_.font_subset) {
      changes += SubsetEmbeddedFonts();
    }
    if (opts_.font_dedup_dicts) {
      changes += DedupFontDicts();
    }
    return changes;
  }

 private:

  int ConvertType1FontsToCff() {
    int converted = 0;
    WalkAllFontDescriptors([&](CPDF_Dictionary* desc) {
      RetainPtr<CPDF_Object> ent = desc->GetMutableObjectFor("FontFile");
      if (!ent || !ent->IsReference()) return;
      const uint32_t t1_objnum = ent->AsReference()->GetRefObjNum();
      if (t1_objnum == 0) return;
      RetainPtr<const CPDF_Object> tgt = doc_->GetIndirectObject(t1_objnum);
      if (!tgt || !tgt->IsStream()) return;
      const CPDF_Stream* t1_stream = tgt->AsStream();
      if (!t1_stream) return;

      auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(
          pdfium::WrapRetain<const CPDF_Stream>(t1_stream));
      acc->LoadAllDataFiltered();
      pdfium::span<const uint8_t> t1_span = acc->GetSpan();
      if (t1_span.empty()) return;

      uint8_t* cff_buf = nullptr;
      size_t cff_len = 0;
      if (!HyperType1ToCff(t1_span.data(), t1_span.size(),
                            &cff_buf, &cff_len) ||
          !cff_buf || cff_len == 0) {
        if (cff_buf) HyperType1Free(cff_buf);
        return;
      }
      DataVector<uint8_t> cff_bytes(cff_buf, cff_buf + cff_len);
      HyperType1Free(cff_buf);

      DataVector<uint8_t> flate_bytes =
          FlateModule::Encode(pdfium::span<const uint8_t>(
              cff_bytes.data(), cff_bytes.size()));
      auto new_dict = pdfium::MakeRetain<CPDF_Dictionary>();
      new_dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
      new_dict->SetNewFor<CPDF_Name>("Subtype", "Type1C");
      auto new_stream = doc_->NewIndirect<CPDF_Stream>(
          std::move(flate_bytes), std::move(new_dict));
      if (!new_stream) return;
      const uint32_t cff_objnum = new_stream->GetObjNum();

      desc->RemoveFor("FontFile");
      desc->SetNewFor<CPDF_Reference>("FontFile3", doc_, cff_objnum);

      lifted_cff_objnums_.insert(cff_objnum);
      ++converted;
    });
    return converted;
  }

  int RemoveStandardFontPrograms() {
    struct DescGroup {
      std::vector<CPDF_Dictionary*> fonts;
      bool all_safe = true;
      bool has_foreign_sharer = false;
      ByteString stripped;
    };
    std::map<uint32_t, DescGroup> groups;
    int dropped = 0;

    auto unembed = [&](CPDF_Dictionary* desc,
                       const std::vector<CPDF_Dictionary*>& fonts,
                       const ByteString& stripped) {
      for (CPDF_Dictionary* f : fonts) {
        f->SetNewFor<CPDF_Name>("BaseFont", stripped);
      }
      desc->SetNewFor<CPDF_Name>("FontName", stripped);
      for (const char* key : {"FontFile", "FontFile2", "FontFile3"}) {
        desc->RemoveFor(key);
      }
      ++dropped;
    };

    const uint32_t last = doc_->GetLastObjNum();
    for (uint32_t i = 1; i <= last; ++i) {
      RetainPtr<CPDF_Object> obj = doc_->GetOrParseIndirectObject(i);
      if (!obj || !obj->IsDictionary()) continue;
      CPDF_Dictionary* font_dict = obj->AsMutableDictionary();
      if (font_dict->GetNameFor("Type") != "Font") continue;

      RetainPtr<CPDF_Object> dref =
          font_dict->GetMutableObjectFor("FontDescriptor");
      if (!dref) continue;

      ByteString subtype = font_dict->GetNameFor("Subtype");
      const bool simple = subtype == "Type1" || subtype == "MMType1" ||
                          subtype == "TrueType";
      ByteString base_font = font_dict->GetNameFor("BaseFont");
      ByteString stripped = StripSubsetPrefix(base_font);
      ByteString sub_name = stripped;
      Std14Class cls = ClassifyStandard14(stripped);
      bool is_std14 = simple && cls != Std14Class::kNone;

      if (!is_std14 && simple && opts_.unembed_aliased_fonts) {
        Std14Class acls = Std14Class::kNone;
        ByteString alias = MapAliasToStandard14(stripped, &acls);
        if (!alias.IsEmpty()) {
          cls = acls;
          sub_name = alias;
          is_std14 = true;
        }
      }
      bool safe = false;
      if (is_std14) {
        safe = SafeToUnembedStandardFont(font_dict, cls,
                                         base_font != stripped);
      }

      if (dref->IsReference()) {
        uint32_t dn = dref->AsReference()->GetRefObjNum();
        if (dn == 0) continue;
        DescGroup& g = groups[dn];
        if (!is_std14) {
          g.has_foreign_sharer = true;
          continue;
        }
        g.fonts.push_back(font_dict);

        if (g.fonts.size() == 1)
          g.stripped = sub_name;
        else if (g.stripped != sub_name)
          g.all_safe = false;
        if (!safe) g.all_safe = false;
      } else if (dref->IsDictionary() && is_std14 && safe) {

        CPDF_Dictionary* desc = dref->AsMutableDictionary();
        const bool has_program =
            desc->KeyExist("FontFile") || desc->KeyExist("FontFile2") ||
            desc->KeyExist("FontFile3");
        if (has_program) {
          unembed(desc, {font_dict}, sub_name);
        }
      }
    }

    for (auto& kv : groups) {
      DescGroup& g = kv.second;
      if (g.fonts.empty() || g.has_foreign_sharer || !g.all_safe) continue;
      RetainPtr<CPDF_Object> dobj = doc_->GetOrParseIndirectObject(kv.first);
      if (!dobj || !dobj->IsDictionary()) continue;
      CPDF_Dictionary* desc = dobj->AsMutableDictionary();
      const bool has_program =
          desc->KeyExist("FontFile") || desc->KeyExist("FontFile2") ||
          desc->KeyExist("FontFile3");
      if (!has_program) continue;
      unembed(desc, g.fonts, g.stripped);
    }
    return dropped;
  }

  int MergeFontPrograms() {
    std::unordered_map<StreamDigest, uint32_t, StreamDigestHash> seen;
    int merged = 0;
    WalkAllFontDescriptors([&](CPDF_Dictionary* desc) {
      for (const char* key : {"FontFile", "FontFile2", "FontFile3"}) {
        RetainPtr<CPDF_Object> entry = desc->GetMutableObjectFor(key);
        if (!entry) continue;

        const CPDF_Stream* stream = nullptr;
        uint32_t this_objnum = 0;
        if (entry->IsReference()) {
          this_objnum = entry->AsReference()->GetRefObjNum();
          RetainPtr<const CPDF_Object> target =
              doc_->GetIndirectObject(this_objnum);
          if (target && target->IsStream()) {
            stream = target->AsStream();
          }
        } else if (entry->IsStream()) {
          stream = entry->AsStream();
          this_objnum = entry->GetObjNum();
        }
        if (!stream || this_objnum == 0) continue;
        StreamDigest d = DigestStream(stream);
        auto it = seen.find(d);
        if (it == seen.end()) {
          seen[d] = this_objnum;
        } else if (it->second != this_objnum) {

          desc->SetNewFor<CPDF_Reference>(key, doc_, it->second);
          ++merged;
        }
      }
    });
    return merged;
  }

  int DedupFontDicts() {

    std::unordered_map<uint64_t, uint32_t> seen;
    int collapsed = 0;
    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(i);
      if (!page_dict) continue;
      RetainPtr<CPDF_Dictionary> resources =
          page_dict->GetMutableDictFor("Resources");
      if (!resources) continue;
      RetainPtr<CPDF_Dictionary> font_map =
          resources->GetMutableDictFor("Font");
      if (!font_map) continue;

      std::vector<ByteString> keys;
      {
        CPDF_DictionaryLocker locker(font_map.Get());
        for (const auto& it : locker) {
          keys.push_back(it.first);
        }
      }
      for (const ByteString& key : keys) {
        RetainPtr<CPDF_Object> entry =
            font_map->GetMutableObjectFor(key.AsStringView());
        if (!entry || !entry->IsReference()) continue;
        uint32_t this_objnum = entry->AsReference()->GetRefObjNum();
        if (this_objnum == 0) continue;
        RetainPtr<const CPDF_Object> target =
            doc_->GetIndirectObject(this_objnum);
        if (!target || !target->IsDictionary()) continue;
        const CPDF_Dictionary* font_dict = target->AsDictionary();
        if (!font_dict->KeyExist("Type") &&
            !font_dict->KeyExist("BaseFont")) {
          continue;
        }
        uint64_t digest = DigestDictForDedup(font_dict);
        auto it = seen.find(digest);
        if (it == seen.end()) {
          seen[digest] = this_objnum;
        } else if (it->second != this_objnum) {
          font_map->SetNewFor<CPDF_Reference>(key, doc_, it->second);
          ++collapsed;
        }
      }
    }
    return collapsed;
  }

  enum FontFileKind {
    kFontFile2_TT,
    kFontFile3_OpenType,
    kFontFile3_BareCff,
  };

  int SubsetEmbeddedFonts() {

    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>
        glyphs_per_fontfile;
    std::unordered_map<uint32_t, FontFileKind> kind_per_fontfile;

    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>
        fontdicts_per_fontfile;

    struct LiftedXlat {
      std::vector<uint8_t> cff;
      CffOnlyFaceCtx ctx;
      hb_face_t* face = nullptr;
      hb_font_t* font = nullptr;
      bool ok = false;
    };
    std::map<uint32_t, LiftedXlat> lifted_xlat;

    std::unordered_set<uint32_t> census_unreliable_fontfiles;
    auto lifted_hb_font_for = [&](uint32_t objnum) -> hb_font_t* {
      auto it = lifted_xlat.find(objnum);
      if (it != lifted_xlat.end())
        return it->second.ok ? it->second.font : nullptr;
      LiftedXlat& lx = lifted_xlat[objnum];
      RetainPtr<CPDF_Object> o = doc_->GetOrParseIndirectObject(objnum);
      RetainPtr<const CPDF_Stream> st =
          o && o->IsStream() ? ToStream(o) : nullptr;
      if (st) {
        auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(st);
        acc->LoadAllDataFiltered();
        pdfium::span<const uint8_t> sp = acc->GetSpan();
        lx.cff.assign(sp.begin(), sp.end());
        if (!lx.cff.empty()) {
          lx.ctx.cff = pdfium::span<const uint8_t>(lx.cff.data(), lx.cff.size());
          lx.face =
              hb_face_create_for_tables(&CffOnlyReferenceTable, &lx.ctx, nullptr);
          if (lx.face) {
            const uint32_t cnt = HyperCffGlyphCount(lx.ctx.cff);
            if (cnt > 0) {
              hb_face_set_glyph_count(lx.face, cnt);
              lx.font = hb_font_create(lx.face);
              lx.ok = lx.font != nullptr;
            }
          }
        }
      }
      return lx.ok ? lx.font : nullptr;
    };
    auto add_glyphs_from_text_obj = [&](CPDF_TextObject* text_obj) {
      if (!text_obj) return;
      RetainPtr<CPDF_Font> font = text_obj->GetFont();
      if (!font) return;
      FontFileKind kind = kFontFile2_TT;
      const uint32_t fontfile_objnum =
          ResolveSubsettableFontFile(font->GetFontDict().Get(), &kind);
      if (fontfile_objnum == 0) return;
      auto& gset = glyphs_per_fontfile[fontfile_objnum];
      kind_per_fontfile[fontfile_objnum] = kind;
      const uint32_t fd_objnum = font->GetFontDict()->GetObjNum();
      if (fd_objnum != 0) {
        fontdicts_per_fontfile[fontfile_objnum].insert(fd_objnum);
      }
      const bool lifted = lifted_cff_objnums_.count(fontfile_objnum) > 0;
      hb_font_t* lifted_font = nullptr;
      RetainPtr<CFX_Face> old_face;
      if (lifted) {
        lifted_font = lifted_hb_font_for(fontfile_objnum);
        CFX_Font* cfx = font->GetFont();
        old_face = cfx ? cfx->GetFace() : nullptr;
        if (!lifted_font || !old_face || !old_face->HasGlyphNames()) {
          census_unreliable_fontfiles.insert(fontfile_objnum);
          return;
        }
      }
      const std::vector<uint32_t>& char_codes = text_obj->GetCharCodes();
      for (uint32_t cc : char_codes) {
        if (cc == CPDF_Font::kInvalidCharCode) continue;
        bool vert = false;
        int gid = font->GlyphFromCharCode(cc, &vert);
        if (gid <= 0) continue;
        if (lifted) {
          ByteString gname =
              old_face->GetGlyphName(static_cast<uint32_t>(gid));
          hb_codepoint_t new_gid = 0;
          if (gname.IsEmpty() ||
              !hb_font_get_glyph_from_name(lifted_font, gname.c_str(),
                                           -1, &new_gid)) {
            census_unreliable_fontfiles.insert(fontfile_objnum);
            return;
          }
          gset.insert(static_cast<uint32_t>(new_gid));
          continue;
        }
        gset.insert(static_cast<uint32_t>(gid));
      }
    };

    std::function<void(CPDF_PageObjectHolder*)> walk_holder =
        [&](CPDF_PageObjectHolder* holder) {
          if (!holder) return;
          const size_t obj_count = holder->GetActivePageObjectCount();
          for (size_t i = 0; i < obj_count; ++i) {
            CPDF_PageObject* obj = holder->GetPageObjectByIndex(i);
            if (!obj) continue;
            if (obj->IsText()) {
              add_glyphs_from_text_obj(obj->AsText());
            } else if (obj->IsForm()) {
              CPDF_FormObject* form_obj = obj->AsForm();
              if (form_obj && form_obj->form()) {
                walk_holder(form_obj->form());
              }
            }
          }
        };
    const int page_count = doc_->GetPageCount();
    for (int page_index = 0; page_index < page_count; ++page_index) {
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(page_index);
      if (!page_dict) continue;
      if (!HyperIsLoadablePageDict(page_dict.Get())) continue;
      auto page = pdfium::MakeRetain<CPDF_Page>(doc_, page_dict);
      page->ParseContent();
      walk_holder(page.Get());

      RetainPtr<CPDF_Dictionary> page_resources =
          page_dict->GetMutableDictFor("Resources");
      auto walk_ap_stream = [&](RetainPtr<CPDF_Stream> ap_stream) {
        if (!ap_stream) return;
        auto form = std::make_unique<CPDF_Form>(doc_, page_resources,
                                                std::move(ap_stream));
        form->ParseContent();
        walk_holder(form.get());
      };
      RetainPtr<CPDF_Array> page_annots =
          page_dict->GetMutableArrayFor("Annots");
      if (page_annots) {
        for (size_t ai = 0; ai < page_annots->size(); ++ai) {
          RetainPtr<CPDF_Dictionary> annot = page_annots->GetMutableDictAt(ai);
          if (!annot) continue;
          RetainPtr<CPDF_Dictionary> ap = annot->GetMutableDictFor("AP");
          if (!ap) continue;

          RetainPtr<CPDF_Stream> n_stream = ap->GetMutableStreamFor("N");
          if (n_stream) {
            walk_ap_stream(std::move(n_stream));
            continue;
          }
          RetainPtr<CPDF_Dictionary> n_states = ap->GetMutableDictFor("N");
          if (!n_states) continue;
          std::vector<ByteString> state_keys;
          {
            CPDF_DictionaryLocker locker(n_states);
            for (const auto& it : locker) {
              state_keys.push_back(it.first);
            }
          }
          for (const ByteString& k : state_keys) {
            walk_ap_stream(n_states->GetMutableStreamFor(k.AsStringView()));
          }
        }
      }
    }

    for (uint32_t bad : census_unreliable_fontfiles) {
      glyphs_per_fontfile.erase(bad);
      if (getenv("HYPER_FONT_DEBUG")) {
        fprintf(stderr,
                "[hyper] font subset obj=%u lifted-CFF census unreliable  - "
                "kept unsubsetted\n",
                bad);
      }
    }
    for (auto& kv : lifted_xlat) {
      if (kv.second.font) hb_font_destroy(kv.second.font);
      if (kv.second.face) hb_face_destroy(kv.second.face);
    }

    int subset_count = 0;
    for (const auto& kv : glyphs_per_fontfile) {
      const uint32_t objnum = kv.first;
      const std::unordered_set<uint32_t>& glyphs = kv.second;
      if (objnum == 0 || glyphs.empty()) continue;
      const FontFileKind kind = kind_per_fontfile[objnum];

      RetainPtr<CPDF_Object> mutable_obj =
          doc_->GetOrParseIndirectObject(objnum);
      if (!mutable_obj || !mutable_obj->IsStream()) continue;
      CPDF_Stream* stream = mutable_obj->AsMutableStream();
      if (!stream) continue;

      auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(
          pdfium::WrapRetain<const CPDF_Stream>(stream));
      acc->LoadAllDataFiltered();
      pdfium::span<const uint8_t> orig_span = acc->GetSpan();
      if (orig_span.empty()) continue;
      const size_t orig_decoded_size = orig_span.size();

      DataVector<uint8_t> subset_bytes =
          SubsetFontProgram(orig_span, glyphs, kind);
      if (getenv("HYPER_FONT_DEBUG")) {
        fprintf(stderr,
                "[hyper] font subset obj=%u kind=%d glyphs=%zu orig=%zu "
                "subset=%zu %s\n",
                objnum, static_cast<int>(kind), glyphs.size(),
                orig_decoded_size, subset_bytes.size(),
                subset_bytes.empty() ? "HB-DECLINED"
                : subset_bytes.size() >= orig_decoded_size ? "GREW-KEEP-ORIG"
                                                            : "commit");
      }

      if (kind == kFontFile2_TT) {
        auto fdit = fontdicts_per_fontfile.find(objnum);
        if (fdit != fontdicts_per_fontfile.end() &&
            FontFileSharersAllCidType2(fdit->second)) {
          std::map<uint32_t, uint32_t> old_to_new;
          DataVector<uint8_t> compact =
              SubsetFontProgramCompactCid(orig_span, glyphs, &old_to_new);
          std::vector<CompactCidMap> maps;
          if (!compact.empty() && !old_to_new.empty() &&
              BuildCompactCidMaps(fdit->second, old_to_new, &maps)) {
            DataVector<uint8_t> compact_flate =
                FlateModule::Encode(pdfium::span<const uint8_t>(
                    compact.data(), compact.size()));
            size_t compact_stored = compact_flate.size();
            for (const CompactCidMap& m : maps)
              compact_stored += m.flate.size();

            size_t retain_stored = stream->GetRawSize();
            DataVector<uint8_t> retain_flate;
            if (!subset_bytes.empty() &&
                subset_bytes.size() < orig_decoded_size) {
              retain_flate = FlateModule::Encode(pdfium::span<const uint8_t>(
                  subset_bytes.data(), subset_bytes.size()));
              if (!retain_flate.empty())
                retain_stored = std::min(retain_stored, retain_flate.size());
            }
            if (!compact_flate.empty() && compact_stored < retain_stored) {

              stream->TakeData(std::move(compact_flate));
              RetainPtr<CPDF_Dictionary> mdict = stream->GetMutableDict();
              if (mdict) {
                mdict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
                mdict->SetNewFor<CPDF_Number>(
                    "Length1", static_cast<int>(compact.size()));
                mdict->RemoveFor("Length2");
                mdict->RemoveFor("Length3");
                mdict->RemoveFor("DecodeParms");
              }

              for (CompactCidMap& m : maps) {
                auto map_dict = doc_->New<CPDF_Dictionary>();
                map_dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
                auto map_stream = doc_->NewIndirect<CPDF_Stream>(
                    std::move(m.flate), std::move(map_dict));
                RetainPtr<CPDF_Object> t0 =
                    doc_->GetOrParseIndirectObject(m.type0_objnum);
                RetainPtr<CPDF_Dictionary> t0d = t0 ? ToDictionary(t0) : nullptr;
                RetainPtr<CPDF_Array> dfonts =
                    t0d ? t0d->GetMutableArrayFor("DescendantFonts") : nullptr;
                RetainPtr<CPDF_Dictionary> desc =
                    dfonts && !dfonts->IsEmpty() ? dfonts->GetMutableDictAt(0)
                                                 : nullptr;
                if (desc) {
                  desc->SetNewFor<CPDF_Reference>("CIDToGIDMap", doc_,
                                                  map_stream->GetObjNum());
                }
              }

              const ByteString subset_tag = MakeSubsetTag(
                  pdfium::span<const uint8_t>(compact.data(), compact.size()));
              for (uint32_t fd_objnum : fdit->second)
                ApplySubsetPrefixToFont(fd_objnum, subset_tag);
              ++subset_count;
              continue;
            }
          }
        }
      }

      if (subset_bytes.empty() ||
          subset_bytes.size() >= orig_decoded_size) {

        continue;
      }
      const size_t subset_decoded_size = subset_bytes.size();

      DataVector<uint8_t> flate_bytes =
          FlateModule::Encode(pdfium::span<const uint8_t>(
              subset_bytes.data(), subset_bytes.size()));

      stream->TakeData(std::move(flate_bytes));
      RetainPtr<CPDF_Dictionary> mut_dict = stream->GetMutableDict();
      if (mut_dict) {

        mut_dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");

        if (kind == kFontFile2_TT) {
          mut_dict->SetNewFor<CPDF_Number>(
              "Length1", static_cast<int>(subset_decoded_size));
        } else {
          mut_dict->RemoveFor("Length1");
        }
        mut_dict->RemoveFor("Length2");
        mut_dict->RemoveFor("Length3");
        mut_dict->RemoveFor("DecodeParms");

      }

      {
        const ByteString subset_tag = MakeSubsetTag(
            pdfium::span<const uint8_t>(subset_bytes.data(), subset_bytes.size()));
        auto it = fontdicts_per_fontfile.find(objnum);
        if (it != fontdicts_per_fontfile.end()) {
          for (uint32_t fd_objnum : it->second) {
            ApplySubsetPrefixToFont(fd_objnum, subset_tag);
          }
        }
      }
      ++subset_count;
    }
    return subset_count;
  }

  void ApplySubsetPrefixToFont(uint32_t font_dict_objnum,
                               const ByteString& tag) {
    if (font_dict_objnum == 0) {
      return;
    }
    RetainPtr<CPDF_Object> obj = doc_->GetOrParseIndirectObject(font_dict_objnum);
    CPDF_Dictionary* font_dict = obj ? obj->AsMutableDictionary() : nullptr;
    if (!font_dict) {
      return;
    }
    auto retag = [&](CPDF_Dictionary* d, const char* key) {
      if (!d) {
        return;
      }
      ByteString cur = d->GetNameFor(key);
      if (cur.IsEmpty()) {
        return;
      }
      d->SetNewFor<CPDF_Name>(key, tag + StripSubsetPrefix(cur));
    };
    retag(font_dict, "BaseFont");
    if (font_dict->GetNameFor("Subtype") == "Type0") {
      RetainPtr<CPDF_Array> dfonts =
          font_dict->GetMutableArrayFor("DescendantFonts");
      if (dfonts && !dfonts->IsEmpty()) {
        RetainPtr<CPDF_Dictionary> desc = dfonts->GetMutableDictAt(0);
        if (desc) {
          retag(desc.Get(), "BaseFont");
          RetainPtr<CPDF_Dictionary> fd =
              desc->GetMutableDictFor("FontDescriptor");
          retag(fd.Get(), "FontName");
        }
      }
    } else {
      RetainPtr<CPDF_Dictionary> fd =
          font_dict->GetMutableDictFor("FontDescriptor");
      retag(fd.Get(), "FontName");
    }
  }

  static DataVector<uint8_t> SubsetFontProgramCompactCid(
      pdfium::span<const uint8_t> orig,
      const std::unordered_set<uint32_t>& glyphs,
      std::map<uint32_t, uint32_t>* out_old_to_new) {
    out_old_to_new->clear();
    hb_blob_t* in_blob = hb_blob_create(
        reinterpret_cast<const char*>(orig.data()),
        static_cast<unsigned int>(orig.size()),
        HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    if (!in_blob)
      return {};
    hb_face_t* in_face = hb_face_create_or_fail(in_blob, 0);
    if (!in_face) {
      hb_blob_destroy(in_blob);
      return {};
    }
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      hb_face_destroy(in_face);
      hb_blob_destroy(in_blob);
      return {};
    }
    hb_set_t* keep = hb_subset_input_glyph_set(input);
    hb_set_add(keep, 0);
    for (uint32_t gid : glyphs)
      hb_set_add(keep, gid);
    DataVector<uint8_t> result;
    hb_subset_plan_t* plan = hb_subset_plan_create_or_fail(in_face, input);
    if (plan) {
      hb_face_t* out_face = hb_subset_plan_execute_or_fail(plan);
      if (out_face) {
        hb_map_t* mapping = hb_subset_plan_old_to_new_glyph_mapping(plan);
        bool map_ok = !!mapping;
        if (map_ok) {
          for (uint32_t gid : glyphs) {
            const hb_codepoint_t ng = hb_map_get(mapping, gid);
            if (ng == HB_MAP_VALUE_INVALID) {
              map_ok = false;
              break;
            }
            (*out_old_to_new)[gid] = ng;
          }
        }
        if (map_ok) {
          hb_blob_t* out_blob = hb_face_reference_blob(out_face);
          if (out_blob) {
            unsigned int out_len = 0;
            const char* out_data = hb_blob_get_data(out_blob, &out_len);
            if (out_data && out_len)
              result = DataVector<uint8_t>(out_data, out_data + out_len);
            hb_blob_destroy(out_blob);
          }
        }
        hb_face_destroy(out_face);
      }
      hb_subset_plan_destroy(plan);
    }
    hb_subset_input_destroy(input);
    hb_face_destroy(in_face);
    hb_blob_destroy(in_blob);
    if (result.empty())
      out_old_to_new->clear();
    return result;
  }

  bool FontFileSharersAllCidType2(
      const std::unordered_set<uint32_t>& fd_objnums) {
    if (fd_objnums.empty())
      return false;
    for (uint32_t fd_objnum : fd_objnums) {
      RetainPtr<CPDF_Object> o = doc_->GetOrParseIndirectObject(fd_objnum);
      RetainPtr<CPDF_Dictionary> d = o ? ToDictionary(o) : nullptr;
      if (!d || d->GetNameFor("Subtype") != "Type0")
        return false;
      RetainPtr<const CPDF_Array> dfonts = d->GetArrayFor("DescendantFonts");
      RetainPtr<const CPDF_Dictionary> desc =
          dfonts && !dfonts->IsEmpty() ? dfonts->GetDictAt(0) : nullptr;
      if (!desc || desc->GetNameFor("Subtype") != "CIDFontType2")
        return false;
    }
    return true;
  }

  struct CompactCidMap {
    uint32_t desc_objnum = 0;
    uint32_t type0_objnum = 0;
    DataVector<uint8_t> flate;
  };

  bool BuildCompactCidMaps(const std::unordered_set<uint32_t>& fd_objnums,
                           const std::map<uint32_t, uint32_t>& old_to_new,
                           std::vector<CompactCidMap>* out) {
    out->clear();
    std::unordered_set<uint32_t> seen_desc;
    for (uint32_t fd_objnum : fd_objnums) {
      RetainPtr<CPDF_Object> o = doc_->GetOrParseIndirectObject(fd_objnum);
      RetainPtr<CPDF_Dictionary> d = o ? ToDictionary(o) : nullptr;
      if (!d)
        return false;
      RetainPtr<const CPDF_Array> dfonts = d->GetArrayFor("DescendantFonts");
      RetainPtr<const CPDF_Dictionary> desc =
          dfonts && !dfonts->IsEmpty() ? dfonts->GetDictAt(0) : nullptr;
      if (!desc)
        return false;
      const uint32_t desc_objnum = desc->GetObjNum();
      if (desc_objnum && !seen_desc.insert(desc_objnum).second)
        continue;

      RetainPtr<const CPDF_Object> cur =
          desc->GetDirectObjectFor("CIDToGIDMap");
      DataVector<uint8_t> old_map;
      if (cur && cur->IsStream()) {
        auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(
            pdfium::WrapRetain(cur->AsStream()));
        acc->LoadAllDataFiltered();
        pdfium::span<const uint8_t> sp = acc->GetSpan();
        old_map = DataVector<uint8_t>(sp.begin(), sp.end());
        if (old_map.empty())
          return false;
      } else if (cur && !(cur->IsName() && cur->GetString() == "Identity")) {
        return false;
      }

      uint32_t max_cid = 0;
      bool any = false;
      if (old_map.empty()) {
        for (const auto& kv : old_to_new) {
          max_cid = std::max(max_cid, kv.first);
          any = true;
        }
      } else {
        const size_t n_cids = old_map.size() / 2;
        for (size_t cid = 0; cid < n_cids; ++cid) {
          const uint32_t og = (uint32_t(old_map[cid * 2]) << 8) |
                              old_map[cid * 2 + 1];
          if (og && old_to_new.count(og)) {
            max_cid = std::max(max_cid, static_cast<uint32_t>(cid));
            any = true;
          }
        }
      }
      if (!any)
        return false;

      DataVector<uint8_t> raw(2 * (static_cast<size_t>(max_cid) + 1));
      for (uint32_t cid = 0; cid <= max_cid; ++cid) {
        uint32_t og = 0;
        if (old_map.empty()) {
          og = cid;
        } else if (static_cast<size_t>(cid) * 2 + 1 < old_map.size()) {
          og = (uint32_t(old_map[cid * 2]) << 8) | old_map[cid * 2 + 1];
        }
        uint32_t ng = 0;
        if (og) {
          auto it = old_to_new.find(og);
          if (it != old_to_new.end())
            ng = it->second;
        }
        raw[cid * 2] = static_cast<uint8_t>(ng >> 8);
        raw[cid * 2 + 1] = static_cast<uint8_t>(ng & 0xFF);
      }
      DataVector<uint8_t> flate = FlateModule::Encode(
          pdfium::span<const uint8_t>(raw.data(), raw.size()));
      if (flate.empty())
        return false;
      CompactCidMap m;
      m.desc_objnum = desc_objnum;
      m.type0_objnum = fd_objnum;
      m.flate = std::move(flate);
      out->push_back(std::move(m));
    }
    return !out->empty();
  }

  struct CffOnlyFaceCtx {
    pdfium::span<const uint8_t> cff;
  };
  static hb_blob_t* CffOnlyReferenceTable(hb_face_t*  ,
                                          hb_tag_t tag,
                                          void* user_data) {
    auto* ctx = static_cast<CffOnlyFaceCtx*>(user_data);
    if (!ctx) return nullptr;

    if (tag == HB_TAG('C', 'F', 'F', ' ')) {
      return hb_blob_create(
          reinterpret_cast<const char*>(ctx->cff.data()),
          static_cast<unsigned int>(ctx->cff.size()),
          HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    }
    return nullptr;
  }

  static uint32_t HyperCffGlyphCount(pdfium::span<const uint8_t> cff) {
    const size_t n = cff.size();
    auto u8 = [&](size_t o) -> uint32_t { return o < n ? cff[o] : 0; };
    auto u16 = [&](size_t o) -> uint32_t {
      return o + 1 < n ? (cff[o] << 8) | cff[o + 1] : 0;
    };
    auto read_off = [&](size_t o, uint32_t sz) -> uint32_t {
      if (sz < 1 || sz > 4 || o + sz > n) return 0;
      uint32_t v = 0;
      for (uint32_t i = 0; i < sz; ++i) v = (v << 8) | cff[o + i];
      return v;
    };
    if (n < 4 || cff[0] != 1)
      return 0;
    size_t pos = u8(2);
    if (pos < 4 || pos >= n) return 0;

    auto skip_index = [&](size_t at, size_t* first_off,
                          size_t* first_len) -> size_t {
      if (at + 2 > n) return 0;
      const uint32_t count = u16(at);
      if (count == 0) return at + 2;
      const uint32_t off_size = u8(at + 2);
      if (off_size < 1 || off_size > 4) return 0;
      const size_t offs = at + 3;
      const size_t data = offs + static_cast<size_t>(count + 1) * off_size;
      const uint32_t first = read_off(offs, off_size);
      const uint32_t last = read_off(offs + static_cast<size_t>(count) * off_size,
                                     off_size);
      if (first < 1 || last < first || data - 1 + last > n) return 0;
      if (first_off) *first_off = data - 1 + first;
      if (first_len) {
        const uint32_t second = read_off(offs + off_size, off_size);
        *first_len = second >= first ? second - first : 0;
      }
      return data - 1 + last;
    };
    pos = skip_index(pos, nullptr, nullptr);
    if (!pos) return 0;
    size_t top_off = 0, top_len = 0;
    if (!skip_index(pos, &top_off, &top_len) || !top_len) return 0;

    size_t p = top_off;
    const size_t end = top_off + top_len;
    int32_t last_operand = -1;
    int32_t charstrings_off = -1;
    while (p < end) {
      const uint32_t b0 = u8(p);
      if (b0 >= 32 && b0 <= 246) { last_operand = static_cast<int32_t>(b0) - 139; p += 1; }
      else if (b0 >= 247 && b0 <= 250) { last_operand = (static_cast<int32_t>(b0) - 247) * 256 + u8(p + 1) + 108; p += 2; }
      else if (b0 >= 251 && b0 <= 254) { last_operand = -(static_cast<int32_t>(b0) - 251) * 256 - static_cast<int32_t>(u8(p + 1)) - 108; p += 2; }
      else if (b0 == 28) { last_operand = static_cast<int16_t>(u16(p + 1)); p += 3; }
      else if (b0 == 29) { last_operand = static_cast<int32_t>((u16(p + 1) << 16) | u16(p + 3)); p += 5; }
      else if (b0 == 30) {
        p += 1;
        while (p < end) { const uint32_t b = u8(p++); if ((b & 0x0F) == 0x0F || (b >> 4) == 0x0F) break; }
      } else if (b0 == 12) { p += 2; last_operand = -1; }
      else { if (b0 == 17) charstrings_off = last_operand; p += 1; last_operand = -1; }
      if (charstrings_off >= 0) break;
    }
    if (charstrings_off < 2 || static_cast<size_t>(charstrings_off) + 2 > n)
      return 0;
    return u16(static_cast<size_t>(charstrings_off));
  }

  static DataVector<uint8_t> SubsetFontProgram(
      pdfium::span<const uint8_t> orig,
      const std::unordered_set<uint32_t>& glyphs,
      FontFileKind kind) {

    hb_blob_t* in_blob = nullptr;
    hb_face_t* in_face = nullptr;
    CffOnlyFaceCtx cff_ctx{};
    if (kind == kFontFile3_BareCff) {

      cff_ctx.cff = orig;
      in_face = hb_face_create_for_tables(
          &CffOnlyReferenceTable, &cff_ctx, nullptr);
      if (in_face) {

        const uint32_t real_count = HyperCffGlyphCount(orig);
        uint32_t max_gid = 0;
        for (uint32_t g : glyphs) {
          if (g > max_gid) max_gid = g;
        }
        hb_face_set_glyph_count(
            in_face, real_count > max_gid ? real_count : max_gid + 1);
      }
    } else {
      in_blob = hb_blob_create(
          reinterpret_cast<const char*>(orig.data()),
          static_cast<unsigned int>(orig.size()),
          HB_MEMORY_MODE_READONLY, nullptr, nullptr);
      if (!in_blob) return {};
      in_face = hb_face_create_or_fail(in_blob, 0);
    }
    if (!in_face) {
      if (in_blob) hb_blob_destroy(in_blob);
      return {};
    }
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      hb_face_destroy(in_face);
      if (in_blob) hb_blob_destroy(in_blob);
      return {};
    }

    hb_subset_input_set_flags(
        input, HB_SUBSET_FLAGS_RETAIN_GIDS | HB_SUBSET_FLAGS_GLYPH_NAMES);
    hb_set_t* keep = hb_subset_input_glyph_set(input);
    hb_set_add(keep, 0);
    for (uint32_t gid : glyphs) hb_set_add(keep, gid);
    hb_face_t* out_face = hb_subset_or_fail(in_face, input);
    hb_subset_input_destroy(input);
    if (!out_face) {
      hb_face_destroy(in_face);
      if (in_blob) hb_blob_destroy(in_blob);
      return {};
    }

    hb_blob_t* out_blob = nullptr;
    if (kind == kFontFile3_BareCff) {
      out_blob = hb_face_reference_table(out_face,
                                          HB_TAG('C', 'F', 'F', ' '));
    } else {
      out_blob = hb_face_reference_blob(out_face);
    }
    hb_face_destroy(out_face);
    hb_face_destroy(in_face);
    if (in_blob) hb_blob_destroy(in_blob);
    if (!out_blob) return {};
    unsigned int out_len = 0;
    const char* out_data = hb_blob_get_data(out_blob, &out_len);
    if (!out_data || out_len == 0) {
      hb_blob_destroy(out_blob);
      return {};
    }

    DataVector<uint8_t> result(out_data, out_data + out_len);
    hb_blob_destroy(out_blob);

    if (kind == kFontFile2_TT) {
      result = EnsureTrueTypeUnicodeCmap(std::move(result), orig, glyphs);
    }
    return result;
  }

  struct SfntTbl {
    uint32_t tag = 0;
    uint32_t off = 0;
    uint32_t len = 0;
  };
  static uint16_t Rd16(pdfium::span<const uint8_t> d, size_t p) {
    return (p + 1 < d.size()) ? ((uint16_t(d[p]) << 8) | d[p + 1]) : 0;
  }
  static uint32_t Rd32(pdfium::span<const uint8_t> d, size_t p) {
    return (p + 3 < d.size())
               ? ((uint32_t(d[p]) << 24) | (uint32_t(d[p + 1]) << 16) |
                  (uint32_t(d[p + 2]) << 8) | d[p + 3])
               : 0;
  }
  static void Wr16(std::vector<uint8_t>* o, uint16_t v) {
    o->push_back(uint8_t(v >> 8));
    o->push_back(uint8_t(v & 0xFF));
  }
  static void Wr32(std::vector<uint8_t>* o, uint32_t v) {
    o->push_back(uint8_t(v >> 24));
    o->push_back(uint8_t((v >> 16) & 0xFF));
    o->push_back(uint8_t((v >> 8) & 0xFF));
    o->push_back(uint8_t(v & 0xFF));
  }
  static bool ReadSfntDir(pdfium::span<const uint8_t> d,
                          std::vector<SfntTbl>* tbls) {
    if (d.size() < 12) return false;
    uint16_t n = Rd16(d, 4);
    if (n == 0 || d.size() < 12u + 16u * n) return false;
    tbls->clear();
    for (uint16_t i = 0; i < n; i++) {
      size_t p = 12 + size_t(i) * 16;
      tbls->push_back({Rd32(d, p), Rd32(d, p + 8), Rd32(d, p + 12)});
    }
    return true;
  }
  static pdfium::span<const uint8_t> FindTable(
      pdfium::span<const uint8_t> d, const std::vector<SfntTbl>& tbls,
      uint32_t tag) {
    for (const auto& t : tbls) {
      if (t.tag == tag && size_t(t.off) + t.len <= d.size()) {
        return d.subspan(t.off, t.len);
      }
    }
    return {};
  }

  static size_t FindUnicodeSubtable(pdfium::span<const uint8_t> cmap) {
    if (cmap.size() < 4) return SIZE_MAX;
    uint16_t n = Rd16(cmap, 2);
    size_t best = SIZE_MAX;
    int best_rank = -1;
    for (uint16_t i = 0; i < n; i++) {
      size_t p = 4 + size_t(i) * 8;
      if (p + 8 > cmap.size()) break;
      uint16_t pid = Rd16(cmap, p);
      uint16_t eid = Rd16(cmap, p + 2);
      uint32_t off = Rd32(cmap, p + 4);
      int rank = -1;
      if (pid == 3 && eid == 10) rank = 4;
      else if (pid == 0 && (eid == 4 || eid == 6)) rank = 3;
      else if (pid == 3 && eid == 1) rank = 3;
      else if (pid == 0) rank = 2;
      if (rank > best_rank && off < cmap.size()) {
        best_rank = rank;
        best = off;
      }
    }
    return best;
  }

  static size_t FindMacRomanSubtable(pdfium::span<const uint8_t> cmap) {
    if (cmap.size() < 4) return SIZE_MAX;
    uint16_t n = Rd16(cmap, 2);
    for (uint16_t i = 0; i < n; i++) {
      size_t p = 4 + size_t(i) * 8;
      if (p + 8 > cmap.size()) break;
      uint16_t pid = Rd16(cmap, p);
      uint16_t eid = Rd16(cmap, p + 2);
      uint32_t off = Rd32(cmap, p + 4);
      if (pid == 1 && eid == 0 && off < cmap.size()) return off;
    }
    return SIZE_MAX;
  }

  static void ParseCmapSubtable(pdfium::span<const uint8_t> cmap, size_t off,
                                std::map<uint32_t, uint32_t>* u2g) {
    if (off + 2 > cmap.size()) return;
    uint16_t fmt = Rd16(cmap, off);
    if (fmt == 0) {
      for (uint32_t c = 0; c < 256; c++) {
        size_t p = off + 6 + c;
        if (p >= cmap.size()) break;
        uint8_t g = cmap[p];
        if (g) (*u2g)[c] = g;
      }
    } else if (fmt == 6) {
      uint16_t first = Rd16(cmap, off + 6);
      uint16_t cnt = Rd16(cmap, off + 8);
      for (uint16_t i = 0; i < cnt; i++) {
        uint16_t g = Rd16(cmap, off + 10 + size_t(i) * 2);
        if (g) (*u2g)[uint32_t(first) + i] = g;
      }
    } else if (fmt == 4) {
      uint16_t seg = Rd16(cmap, off + 6) / 2;
      if (seg == 0) return;
      size_t end_o = off + 14;
      size_t start_o = end_o + size_t(seg) * 2 + 2;
      size_t delta_o = start_o + size_t(seg) * 2;
      size_t range_o = delta_o + size_t(seg) * 2;
      for (uint16_t s = 0; s < seg; s++) {
        uint16_t end = Rd16(cmap, end_o + size_t(s) * 2);
        uint16_t start = Rd16(cmap, start_o + size_t(s) * 2);
        uint16_t delta = Rd16(cmap, delta_o + size_t(s) * 2);
        uint16_t ro = Rd16(cmap, range_o + size_t(s) * 2);
        if (start > end) continue;
        for (uint32_t c = start; c <= end; c++) {
          if (c == 0xFFFF) continue;
          uint16_t g;
          if (ro == 0) {
            g = uint16_t((c + delta) & 0xFFFF);
          } else {
            size_t gp = range_o + size_t(s) * 2 + ro + size_t(c - start) * 2;
            uint16_t gi = Rd16(cmap, gp);
            g = gi ? uint16_t((gi + delta) & 0xFFFF) : 0;
          }
          if (g) (*u2g)[c] = g;
        }
      }
    } else if (fmt == 12) {
      uint32_t ng = Rd32(cmap, off + 12);
      for (uint32_t i = 0; i < ng; i++) {
        size_t gp = off + 16 + size_t(i) * 12;
        if (gp + 12 > cmap.size()) break;
        uint32_t sc = Rd32(cmap, gp);
        uint32_t ec = Rd32(cmap, gp + 4);
        uint32_t sg = Rd32(cmap, gp + 8);
        if (ec < sc) continue;
        for (uint32_t c = sc; c <= ec && c <= 0xFFFF; c++) {
          (*u2g)[c] = sg + (c - sc);
        }
      }
    }
  }

  static std::vector<uint8_t> BuildFormat4Subtable(
      const std::map<uint32_t, uint32_t>& m) {
    std::vector<std::pair<uint16_t, uint16_t>> e;
    for (const auto& kv : m) {
      if (kv.first <= 0xFFFE && kv.second && kv.second <= 0xFFFF) {
        e.push_back({uint16_t(kv.first), uint16_t(kv.second)});
      }
    }
    size_t seg = e.size() + 1;
    uint16_t seg_x2 = uint16_t(seg * 2);
    uint16_t pw = 1, es = 0;
    while (uint32_t(pw) * 2 <= seg) { pw = uint16_t(pw * 2); es++; }
    uint16_t sr = uint16_t(pw * 2);
    std::vector<uint8_t> sub;
    Wr16(&sub, 4);
    size_t len_pos = sub.size();
    Wr16(&sub, 0);
    Wr16(&sub, 0);
    Wr16(&sub, seg_x2);
    Wr16(&sub, sr);
    Wr16(&sub, es);
    Wr16(&sub, uint16_t(seg_x2 - sr));
    for (const auto& p : e) Wr16(&sub, p.first);
    Wr16(&sub, 0xFFFF);
    Wr16(&sub, 0);
    for (const auto& p : e) Wr16(&sub, p.first);
    Wr16(&sub, 0xFFFF);
    for (const auto& p : e)
      Wr16(&sub, uint16_t((p.second - p.first) & 0xFFFF));
    Wr16(&sub, 1);
    for (size_t i = 0; i < seg; i++) Wr16(&sub, 0);
    sub[len_pos] = uint8_t(sub.size() >> 8);
    sub[len_pos + 1] = uint8_t(sub.size() & 0xFF);
    return sub;
  }

  static std::vector<uint8_t> BuildFormat0Subtable(
      const std::map<uint32_t, uint32_t>& m) {
    std::array<uint8_t, 256> glyph_ids{};
    for (const auto& kv : m) {
      if (kv.second == 0) continue;
      if (kv.first > 0xFF || kv.second > 0xFF) return {};
      glyph_ids[kv.first] = uint8_t(kv.second);
    }
    std::vector<uint8_t> sub;
    Wr16(&sub, 0);
    Wr16(&sub, 262);
    Wr16(&sub, 0);
    sub.insert(sub.end(), glyph_ids.begin(), glyph_ids.end());
    return sub;
  }
  static uint32_t TableChecksum(const uint8_t* p, size_t len) {
    uint32_t sum = 0;
    size_t words = (len + 3) / 4;
    for (size_t i = 0; i < words; i++) {
      uint32_t w = 0;
      for (size_t b = 0; b < 4; b++) {
        size_t idx = i * 4 + b;
        w = (w << 8) | (idx < len ? p[idx] : 0);
      }
      sum += w;
    }
    return sum;
  }

  static DataVector<uint8_t> RebuildSfntWithCmap(
      pdfium::span<const uint8_t> sfnt, const std::vector<SfntTbl>& tbls,
      const std::vector<uint8_t>& new_cmap) {
    constexpr uint32_t kCmap = 0x636D6170;
    constexpr uint32_t kHead = 0x68656164;
    std::map<uint32_t, std::vector<uint8_t>> data;
    for (const auto& t : tbls) {
      if (size_t(t.off) + t.len <= sfnt.size()) {
        data[t.tag].assign(sfnt.begin() + t.off, sfnt.begin() + t.off + t.len);
      }
    }
    data[kCmap] = new_cmap;
    uint16_t n = uint16_t(data.size());
    uint16_t pw = 1, es = 0;
    while (uint32_t(pw) * 2 <= n) { pw = uint16_t(pw * 2); es++; }
    uint16_t sr = uint16_t(pw * 16);
    std::vector<uint8_t> out;
    Wr32(&out, 0x00010000);
    Wr16(&out, n);
    Wr16(&out, sr);
    Wr16(&out, es);
    Wr16(&out, uint16_t(n * 16 - sr));
    size_t dir_off = out.size();
    out.resize(out.size() + size_t(n) * 16, 0);
    struct Rec {
      uint32_t tag, off, len, csum;
    };
    std::vector<Rec> recs;
    for (auto& kv : data) {
      while (out.size() % 4) out.push_back(0);
      uint32_t off = uint32_t(out.size());
      out.insert(out.end(), kv.second.begin(), kv.second.end());
      uint32_t csum = TableChecksum(kv.second.data(), kv.second.size());
      recs.push_back({kv.first, off, uint32_t(kv.second.size()), csum});
    }
    while (out.size() % 4) out.push_back(0);
    size_t p = dir_off;
    for (const auto& r : recs) {
      out[p] = uint8_t(r.tag >> 24);
      out[p + 1] = uint8_t((r.tag >> 16) & 0xFF);
      out[p + 2] = uint8_t((r.tag >> 8) & 0xFF);
      out[p + 3] = uint8_t(r.tag & 0xFF);
      out[p + 4] = uint8_t(r.csum >> 24);
      out[p + 5] = uint8_t((r.csum >> 16) & 0xFF);
      out[p + 6] = uint8_t((r.csum >> 8) & 0xFF);
      out[p + 7] = uint8_t(r.csum & 0xFF);
      out[p + 8] = uint8_t(r.off >> 24);
      out[p + 9] = uint8_t((r.off >> 16) & 0xFF);
      out[p + 10] = uint8_t((r.off >> 8) & 0xFF);
      out[p + 11] = uint8_t(r.off & 0xFF);
      out[p + 12] = uint8_t(r.len >> 24);
      out[p + 13] = uint8_t((r.len >> 16) & 0xFF);
      out[p + 14] = uint8_t((r.len >> 8) & 0xFF);
      out[p + 15] = uint8_t(r.len & 0xFF);
      p += 16;
    }

    for (const auto& r : recs) {
      if (r.tag == kHead && size_t(r.off) + 12 <= out.size()) {
        out[r.off + 8] = out[r.off + 9] = out[r.off + 10] = out[r.off + 11] = 0;
        uint32_t file_sum = TableChecksum(out.data(), out.size());
        uint32_t adj = 0xB1B0AFBAu - file_sum;
        out[r.off + 8] = uint8_t(adj >> 24);
        out[r.off + 9] = uint8_t((adj >> 16) & 0xFF);
        out[r.off + 10] = uint8_t((adj >> 8) & 0xFF);
        out[r.off + 11] = uint8_t(adj & 0xFF);
        break;
      }
    }
    return DataVector<uint8_t>(out.begin(), out.end());
  }

  static DataVector<uint8_t> EnsureTrueTypeUnicodeCmap(
      DataVector<uint8_t> subset, pdfium::span<const uint8_t> orig,
      const std::unordered_set<uint32_t>& keep_gids) {
    pdfium::span<const uint8_t> sub_span(subset.data(), subset.size());
    std::vector<SfntTbl> sub_tbls;
    if (!ReadSfntDir(sub_span, &sub_tbls)) return subset;
    constexpr uint32_t kCmap = 0x636D6170;
    pdfium::span<const uint8_t> sub_cmap = FindTable(sub_span, sub_tbls, kCmap);

    if (!sub_cmap.empty() && FindUnicodeSubtable(sub_cmap) != SIZE_MAX) {
      return subset;
    }

    std::vector<SfntTbl> orig_tbls;
    if (!ReadSfntDir(orig, &orig_tbls)) return {};
    pdfium::span<const uint8_t> orig_cmap = FindTable(orig, orig_tbls, kCmap);
    if (orig_cmap.empty()) return {};
    size_t uni_off = FindUnicodeSubtable(orig_cmap);
    if (uni_off != SIZE_MAX) {

      std::map<uint32_t, uint32_t> u2g;
      ParseCmapSubtable(orig_cmap, uni_off, &u2g);
      std::map<uint32_t, uint32_t> kept;
      for (const auto& kv : u2g) {
        if (keep_gids.count(kv.second)) kept[kv.first] = kv.second;
      }
      if (kept.empty()) return {};
      std::vector<uint8_t> sub4 = BuildFormat4Subtable(kept);
      std::vector<uint8_t> cmap_tbl;
      Wr16(&cmap_tbl, 0);
      Wr16(&cmap_tbl, 1);
      Wr16(&cmap_tbl, 3);
      Wr16(&cmap_tbl, 1);
      Wr32(&cmap_tbl, 12);
      cmap_tbl.insert(cmap_tbl.end(), sub4.begin(), sub4.end());
      return RebuildSfntWithCmap(sub_span, sub_tbls, cmap_tbl);
    }

    size_t mac_off = FindMacRomanSubtable(orig_cmap);
    if (mac_off == SIZE_MAX) return {};
    std::map<uint32_t, uint32_t> c2g;
    ParseCmapSubtable(orig_cmap, mac_off, &c2g);
    std::map<uint32_t, uint32_t> kept;
    for (const auto& kv : c2g) {
      if (keep_gids.count(kv.second)) kept[kv.first] = kv.second;
    }
    if (kept.empty()) return {};
    std::vector<uint8_t> sub0 = BuildFormat0Subtable(kept);
    if (sub0.empty()) return {};
    std::vector<uint8_t> cmap_tbl;
    Wr16(&cmap_tbl, 0);
    Wr16(&cmap_tbl, 1);
    Wr16(&cmap_tbl, 1);
    Wr16(&cmap_tbl, 0);
    Wr32(&cmap_tbl, 12);
    cmap_tbl.insert(cmap_tbl.end(), sub0.begin(), sub0.end());
    return RebuildSfntWithCmap(sub_span, sub_tbls, cmap_tbl);
  }

  uint32_t ResolveSubsettableFontFile(
      const CPDF_Dictionary* font_dict,
      FontFileKind* out_kind) const {
    if (!font_dict) return 0;
    const CPDF_Dictionary* desc_dict = nullptr;
    ByteString subtype = font_dict->GetNameFor("Subtype");
    if (subtype == "Type0") {
      RetainPtr<const CPDF_Object> dfonts =
          font_dict->GetObjectFor("DescendantFonts");
      if (!dfonts) return 0;
      const CPDF_Array* arr = ToArray(dfonts->GetDirect().Get());
      if (!arr || arr->IsEmpty()) return 0;
      RetainPtr<const CPDF_Object> df = arr->GetObjectAt(0);
      if (!df) return 0;
      RetainPtr<const CPDF_Object> df_direct = df->GetDirect();
      if (!df_direct || !df_direct->IsDictionary()) return 0;
      const CPDF_Dictionary* descendant = df_direct->AsDictionary();
      RetainPtr<const CPDF_Object> fd =
          descendant->GetObjectFor("FontDescriptor");
      if (!fd) return 0;
      RetainPtr<const CPDF_Object> fd_direct = fd->GetDirect();
      if (!fd_direct || !fd_direct->IsDictionary()) return 0;
      desc_dict = fd_direct->AsDictionary();
    } else {
      RetainPtr<const CPDF_Object> fd =
          font_dict->GetObjectFor("FontDescriptor");
      if (!fd) return 0;
      RetainPtr<const CPDF_Object> fd_direct = fd->GetDirect();
      if (!fd_direct || !fd_direct->IsDictionary()) return 0;
      desc_dict = fd_direct->AsDictionary();
    }
    if (!desc_dict) return 0;

    RetainPtr<const CPDF_Object> ff2 = desc_dict->GetObjectFor("FontFile2");
    if (ff2 && ff2->IsReference()) {
      if (out_kind) *out_kind = kFontFile2_TT;
      return ff2->AsReference()->GetRefObjNum();
    }

    RetainPtr<const CPDF_Object> ff3 = desc_dict->GetObjectFor("FontFile3");
    if (ff3 && ff3->IsReference()) {
      uint32_t ff3_objnum = ff3->AsReference()->GetRefObjNum();
      RetainPtr<const CPDF_Object> ff3_target =
          doc_->GetIndirectObject(ff3_objnum);
      if (ff3_target && ff3_target->IsStream()) {
        RetainPtr<const CPDF_Dictionary> ff3_dict =
            ff3_target->AsStream()->GetDict();
        if (ff3_dict) {
          ByteString ff3_subtype = ff3_dict->GetNameFor("Subtype");
          if (ff3_subtype == "OpenType") {
            if (out_kind) *out_kind = kFontFile3_OpenType;
            return ff3_objnum;
          }
          if (ff3_subtype == "Type1C" ||
              ff3_subtype == "CIDFontType0C") {
            if (out_kind) *out_kind = kFontFile3_BareCff;
            return ff3_objnum;
          }
        }
      }
    }
    return 0;
  }

  template <typename Fn>
  void WalkAllFontDescriptors(Fn fn) {
    std::unordered_set<uint32_t> visited;
    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(i);
      if (!page_dict) continue;
      RetainPtr<CPDF_Dictionary> resources =
          page_dict->GetMutableDictFor("Resources");
      if (!resources) continue;
      RetainPtr<CPDF_Dictionary> font_map =
          resources->GetMutableDictFor("Font");
      if (!font_map) continue;
      CPDF_DictionaryLocker locker(font_map.Get());
      for (const auto& fp : locker) {
        RetainPtr<const CPDF_Object> font_obj_const = fp.second;
        if (!font_obj_const) continue;
        uint32_t font_objnum = 0;
        const CPDF_Dictionary* font_dict = nullptr;
        if (font_obj_const->IsReference()) {
          font_objnum = font_obj_const->AsReference()->GetRefObjNum();
          if (font_objnum && !visited.insert(font_objnum).second) continue;
          RetainPtr<const CPDF_Object> target =
              doc_->GetIndirectObject(font_objnum);
          if (target && target->IsDictionary()) {
            font_dict = target->AsDictionary();
          }
        } else if (font_obj_const->IsDictionary()) {
          font_dict = font_obj_const->AsDictionary();
        }
        if (!font_dict) continue;
        RetainPtr<const CPDF_Object> desc_const =
            font_dict->GetObjectFor("FontDescriptor");
        if (!desc_const) {

          RetainPtr<const CPDF_Object> dfonts =
              font_dict->GetObjectFor("DescendantFonts");
          const CPDF_Array* arr = ToArray(dfonts.Get());
          if (arr) {
            for (size_t k = 0; k < arr->size(); ++k) {
              RetainPtr<const CPDF_Object> df = arr->GetObjectAt(k);
              const CPDF_Dictionary* df_dict =
                  df ? df->GetDirect()->AsDictionary() : nullptr;
              if (df_dict) {
                RetainPtr<const CPDF_Object> d =
                    df_dict->GetObjectFor("FontDescriptor");
                if (d && d->IsReference()) {
                  uint32_t dn = d->AsReference()->GetRefObjNum();
                  if (dn && visited.insert(dn).second) {
                    RetainPtr<CPDF_Object> mutable_obj =
                        doc_->GetOrParseIndirectObject(dn);
                    if (mutable_obj && mutable_obj->IsDictionary()) {
                      fn(mutable_obj->AsMutableDictionary());
                    }
                  }
                }
              }
            }
          }
          continue;
        }
        uint32_t desc_objnum = 0;
        if (desc_const->IsReference()) {
          desc_objnum = desc_const->AsReference()->GetRefObjNum();
          if (desc_objnum && visited.insert(desc_objnum).second) {
            RetainPtr<CPDF_Object> mutable_obj =
                doc_->GetOrParseIndirectObject(desc_objnum);
            if (mutable_obj && mutable_obj->IsDictionary()) {
              fn(mutable_obj->AsMutableDictionary());
            }
          }
        }
      }
    }
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;

  std::unordered_set<uint32_t> lifted_cff_objnums_;
};

static RetainPtr<CPDF_Object> HyperIndexedBaseColorSpace(
    const CPDF_Object* cs) {
  if (!cs) return nullptr;
  if (const CPDF_Name* name = cs->AsName()) {
    ByteString n = name->GetString();
    if (n == "DeviceRGB" || n == "RGB") return cs->Clone();
    return nullptr;
  }
  const CPDF_Array* arr = cs->AsArray();
  if (!arr || arr->size() < 1) return nullptr;
  RetainPtr<const CPDF_Object> head_obj = arr->GetObjectAt(0);
  const CPDF_Name* head = head_obj ? head_obj->AsName() : nullptr;
  if (!head) return nullptr;
  ByteString fam = head->GetString();

  return nullptr;
}

bool HyperTryEmitIndexed(CPDF_Document* doc,
                         RetainPtr<CPDF_Image> image,
                         const RetainPtr<CFX_DIBitmap>& bitmap,
                         bool pdfa_mode) {
  if (!doc || !image || !bitmap) return false;
  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  if (w < 1 || h < 1) return false;

  if (w < 16 || h < 16) return false;
  const int bpp = bitmap->GetBPP();

  if (bpp != 24 && bpp != 32) return false;

  RetainPtr<CPDF_Object> indexed_base;
  {
    RetainPtr<const CPDF_Stream> st = image->GetStream();
    RetainPtr<const CPDF_Dictionary> d = st ? st->GetDict() : nullptr;
    RetainPtr<const CPDF_Object> cs_obj =
        d ? d->GetDirectObjectFor("ColorSpace") : nullptr;
    indexed_base = HyperIndexedBaseColorSpace(cs_obj.Get());
  }
  if (!indexed_base) return false;

  std::unordered_map<uint32_t, uint8_t> palette_index;
  palette_index.reserve(257);
  std::vector<uint32_t> palette_argb;
  palette_argb.reserve(256);

  DataVector<uint8_t> indices(static_cast<size_t>(w) * h);
  const int stride = bitmap->GetPitch();
  const int step = bpp == 24 ? 3 : 4;
  pdfium::span<const uint8_t> src_span = bitmap->GetBuffer();
  size_t idx_pos = 0;
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = src_span.data() + y * stride;
    for (int x = 0; x < w; ++x) {
      const uint8_t* p = row + x * step;

      uint32_t key = (uint32_t(p[0]) << 0) | (uint32_t(p[1]) << 8) |
                     (uint32_t(p[2]) << 16);
      if (bpp == 32) {
        key |= (uint32_t(p[3]) << 24);
      }
      auto it = palette_index.find(key);
      if (it == palette_index.end()) {
        if (palette_argb.size() >= 256) {

          return false;
        }
        uint8_t idx = static_cast<uint8_t>(palette_argb.size());
        palette_argb.push_back(key);
        palette_index.emplace(key, idx);
        indices[idx_pos++] = idx;
      } else {
        indices[idx_pos++] = it->second;
      }
    }
  }

  const size_t palette_count = palette_argb.size();
  DataVector<uint8_t> palette_bytes(palette_count * 3);
  for (size_t i = 0; i < palette_count; ++i) {

    palette_bytes[i * 3 + 0] = (palette_argb[i] >> 16) & 0xFF;
    palette_bytes[i * 3 + 1] = (palette_argb[i] >> 8) & 0xFF;
    palette_bytes[i * 3 + 2] = (palette_argb[i] >> 0) & 0xFF;
  }

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

  if (!pdfa_mode)
    dict->SetNewFor<CPDF_Boolean>("Interpolate", true);

  auto cs = dict->SetNewFor<CPDF_Array>("ColorSpace");
  cs->AppendNew<CPDF_Name>("Indexed");
  cs->Append(std::move(indexed_base));
  cs->AppendNew<CPDF_Number>(static_cast<int>(palette_count - 1));
  cs->AppendNew<CPDF_String>(pdfium::span<const uint8_t>(palette_bytes),
                              CPDF_String::DataType::kIsHex);

  {
    RetainPtr<const CPDF_Stream> os_probe = image->GetStream();
    const size_t orig_stored = os_probe ? os_probe->GetRawSize() : 0;
    if (orig_stored > 0) {
      DataVector<uint8_t> probe = FlateModule::Encode(
          pdfium::span<const uint8_t>(indices.data(), indices.size()));
      const size_t projected =
          (probe.empty() ? indices.size() : probe.size()) +
          palette_bytes.size() + 256;
      if (projected >= orig_stored)
        return false;
    }
  }

  RetainPtr<CPDF_Stream> new_stream =
      pdfium::MakeRetain<CPDF_Stream>(std::move(indices), std::move(dict));

  RetainPtr<const CPDF_Stream> orig_stream = image->GetStream();
  if (!orig_stream) {
    return false;
  }
  uint32_t orig_objnum = orig_stream->GetObjNum();
  if (orig_objnum == 0) {
    return false;
  }

  new_stream->SetGenNum(orig_stream->GetGenNum() + 1);
  bool swapped = doc->ReplaceIndirectObjectIfHigherGeneration(
      orig_objnum, std::move(new_stream));
  return swapped;
}

bool HyperTryEmitLossyIndexed(CPDF_Document* doc,
                              RetainPtr<CPDF_Image> image,
                              const RetainPtr<CFX_DIBitmap>& bitmap,
                              double max_mean_err,
                              int jpeg_quality,
                              int jpeg_subsample,
                              bool pdfa_mode) {
  if (!doc || !image || !bitmap) return false;
  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  if (w < 16 || h < 16) return false;
  const int bpp = bitmap->GetBPP();
  if (bpp != 24 && bpp != 32) return false;

  RetainPtr<CPDF_Object> indexed_base;
  {
    RetainPtr<const CPDF_Stream> st = image->GetStream();
    RetainPtr<const CPDF_Dictionary> d = st ? st->GetDict() : nullptr;
    RetainPtr<const CPDF_Object> cs_obj =
        d ? d->GetDirectObjectFor("ColorSpace") : nullptr;
    indexed_base = HyperIndexedBaseColorSpace(cs_obj.Get());
  }
  if (!indexed_base) return false;

  pdfium::span<const uint8_t> src_span = bitmap->GetBuffer();
  const int stride = bitmap->GetPitch();
  uint8_t* q_indices = nullptr;
  uint8_t* q_palette = nullptr;
  int q_ncolors = 0;
  double mean_err = 0.0;
  if (!HyperQuantizeRgbToIndexed(src_span.data(), w, h, stride, bpp,
                                  256, &q_indices, &q_palette,
                                 &q_ncolors, &mean_err)) {
    return false;
  }

  if (mean_err > max_mean_err || q_ncolors < 1) {
    HyperJbig2Free(q_indices);
    HyperJbig2Free(q_palette);
    return false;
  }

  DataVector<uint8_t> palette_bytes(static_cast<size_t>(q_ncolors) * 3);
  memcpy(palette_bytes.data(), q_palette, palette_bytes.size());
  DataVector<uint8_t> index_plane(static_cast<size_t>(w) * h);
  memcpy(index_plane.data(), q_indices, index_plane.size());
  HyperJbig2Free(q_indices);
  HyperJbig2Free(q_palette);

  DataVector<uint8_t> flate = FlateModule::Encode(
      pdfium::span<const uint8_t>(index_plane.data(), index_plane.size()));
  if (flate.empty()) return false;
  RetainPtr<const CPDF_Stream> orig_stream = image->GetStream();
  if (!orig_stream) return false;
  const size_t orig_size = orig_stream->GetRawSize();

  const size_t new_size = flate.size() + static_cast<size_t>(q_ncolors) * 6;

  size_t alt_size = orig_size;
  {
    uint8_t* jl = nullptr;
    size_t jl_len = 0;
    if (HyperJpegliEncode(src_span.data(), w, h, stride, bpp / 8,
                          bpp >= 24 ? 1 : 0, jpeg_quality,  0,
                          jpeg_subsample ? 1 : 0, &jl, &jl_len) &&
        jl && jl_len) {
      if (alt_size == 0 || jl_len < alt_size) alt_size = jl_len;
    }
    if (jl) HyperJpegliFree(jl);
  }
  if (alt_size == 0 || new_size >= alt_size) return false;

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
  if (!pdfa_mode)
    dict->SetNewFor<CPDF_Boolean>("Interpolate", true);
  dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
  auto cs = dict->SetNewFor<CPDF_Array>("ColorSpace");
  cs->AppendNew<CPDF_Name>("Indexed");
  cs->Append(std::move(indexed_base));
  cs->AppendNew<CPDF_Number>(static_cast<int>(q_ncolors - 1));
  cs->AppendNew<CPDF_String>(pdfium::span<const uint8_t>(palette_bytes),
                              CPDF_String::DataType::kIsHex);

  return image->OverwriteStreamInPlace(std::move(flate), std::move(dict),
                                        false);
}

RetainPtr<CFX_DIBitmap> HyperClipBitmapToVisible(
    RetainPtr<CFX_DIBitmap> bitmap,
    const CFX_Matrix& image_ctm,
    const CPDF_ClipPath& clip,
    CFX_Matrix* out_matrix) {
  *out_matrix = image_ctm;

  if (!bitmap || !clip.HasRef() || clip.GetPathCount() == 0) {
    return bitmap;
  }

  if (image_ctm.b != 0.0f || image_ctm.c != 0.0f) {
    return bitmap;
  }

  CFX_FloatRect image_rect(0.0f, 0.0f, 1.0f, 1.0f);
  image_rect = image_ctm.TransformRect(image_rect);
  if (image_rect.Width() <= 0 || image_rect.Height() <= 0) {
    return bitmap;
  }

  CFX_FloatRect clip_box = clip.GetClipBox();
  if (clip_box.Width() <= 0 || clip_box.Height() <= 0) {
    return bitmap;
  }

  CFX_FloatRect visible = image_rect;
  visible.Intersect(clip_box);
  if (visible.Width() <= 0 || visible.Height() <= 0) {
    return bitmap;
  }

  const float orig_area = image_rect.Width() * image_rect.Height();
  const float vis_area = visible.Width() * visible.Height();
  if (vis_area / orig_area > 0.9f) {
    return bitmap;
  }

  CFX_Matrix inv = image_ctm.GetInverse();
  CFX_FloatRect visible_natural = inv.TransformRect(visible);

  const int src_w = bitmap->GetWidth();
  const int src_h = bitmap->GetHeight();
  float l = std::max(0.0f, visible_natural.left);
  float b = std::max(0.0f, visible_natural.bottom);
  float r = std::min(1.0f, visible_natural.right);
  float t = std::min(1.0f, visible_natural.top);
  if (l >= r || b >= t) return bitmap;

  int px_l = std::max(0, static_cast<int>(std::floor(l * src_w)));
  int px_t = std::max(0, static_cast<int>(std::floor((1.0f - t) * src_h)));
  int px_r = std::min(src_w, static_cast<int>(std::ceil(r * src_w)));
  int px_b = std::min(src_h, static_cast<int>(std::ceil((1.0f - b) * src_h)));
  int new_w = px_r - px_l;
  int new_h = px_b - px_t;
  if (new_w < 2 || new_h < 2) return bitmap;

  auto cropped = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!cropped->Create(new_w, new_h, bitmap->GetFormat())) {
    return bitmap;
  }
  const int bpp = bitmap->GetBPP() / 8;
  if (bpp < 1) {
    return bitmap;
  }
  const int src_pitch = bitmap->GetPitch();
  const int dst_pitch = cropped->GetPitch();
  pdfium::span<const uint8_t> src_span = bitmap->GetBuffer();
  pdfium::span<uint8_t> dst_span = cropped->GetWritableBuffer();
  if (src_span.empty() || dst_span.empty()) {
    return bitmap;
  }

  if (bitmap->HasPalette()) {
    cropped->SetPalette(bitmap->GetPaletteSpan());
  }
  for (int row = 0; row < new_h; ++row) {
    const uint8_t* src_row = src_span.data() +
                             static_cast<size_t>(px_t + row) * src_pitch +
                             static_cast<size_t>(px_l) * bpp;
    uint8_t* dst_row = dst_span.data() + static_cast<size_t>(row) * dst_pitch;
    std::memcpy(dst_row, src_row, static_cast<size_t>(new_w) * bpp);
  }

  for (int gy = 0; gy < 8; ++gy) {
    for (int gx = 0; gx < 8; ++gx) {
      const int sx = gx * (new_w - 1) / 7;
      const int sy = gy * (new_h - 1) / 7;
      const uint8_t* a = src_span.data() +
                         static_cast<size_t>(px_t + sy) * src_pitch +
                         static_cast<size_t>(px_l + sx) * bpp;
      const uint8_t* b = dst_span.data() +
                         static_cast<size_t>(sy) * dst_pitch +
                         static_cast<size_t>(sx) * bpp;
      if (std::memcmp(a, b, bpp) != 0) {
        fprintf(stderr, "[hyper] clip crop verify FAILED  - clip skipped\n");
        return bitmap;
      }
    }
  }

  *out_matrix = CFX_Matrix(
      visible.Width(), 0.0f, 0.0f, visible.Height(), visible.left,
      visible.bottom);
  return cropped;
}

RetainPtr<CFX_DIBitmap> HyperConvertToICCGray(
    const RetainPtr<CFX_DIBitmap>& src) {
  if (!src) return nullptr;
  const int w = src->GetWidth();
  const int h = src->GetHeight();
  const int bpp = src->GetBPP();
  if (w < 1 || h < 1) return nullptr;

  if (bpp != 24 && bpp != 32) return src;

  auto out = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!out->Create(w, h, FXDIB_Format::k8bppRgb)) return nullptr;

  cmsHPROFILE src_profile = cmsCreate_sRGBProfile();
  cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
  cmsToneCurve* gamma = cmsBuildGamma(nullptr, 2.2);
  cmsHPROFILE dst_profile = gamma ? cmsCreateGrayProfile(&d65, gamma) : nullptr;
  if (gamma) cmsFreeToneCurve(gamma);
  if (!src_profile || !dst_profile) {
    if (src_profile) cmsCloseProfile(src_profile);
    if (dst_profile) cmsCloseProfile(dst_profile);
    return nullptr;
  }

  const cmsUInt32Number src_type = (bpp == 24) ? TYPE_BGR_8 : TYPE_BGRA_8;
  cmsHTRANSFORM xform = cmsCreateTransform(
      src_profile, src_type, dst_profile, TYPE_GRAY_8,
      INTENT_PERCEPTUAL, 0);
  cmsCloseProfile(src_profile);
  cmsCloseProfile(dst_profile);
  if (!xform) return nullptr;

  const int src_stride = src->GetPitch();
  const int dst_stride = out->GetPitch();
  pdfium::span<const uint8_t> src_buf = src->GetBuffer();
  pdfium::span<uint8_t> dst_buf = out->GetWritableBuffer();
  if (src_buf.empty() || dst_buf.empty()) {
    cmsDeleteTransform(xform);
    return nullptr;
  }
  for (int y = 0; y < h; ++y) {
    cmsDoTransform(xform, src_buf.data() + y * src_stride,
                   dst_buf.data() + y * dst_stride, w);
  }
  cmsDeleteTransform(xform);
  return out;
}

bool HyperConvertAndEmitCmyk(CPDF_Document* doc,
                              RetainPtr<CPDF_Image> image,
                              const RetainPtr<CFX_DIBitmap>& src) {
  if (!doc || !image || !src) return false;
  const int w = src->GetWidth();
  const int h = src->GetHeight();
  const int bpp = src->GetBPP();
  if (w < 1 || h < 1) return false;
  if (bpp != 24 && bpp != 32) return false;

  cmsHPROFILE src_profile = cmsCreate_sRGBProfile();
  cmsHPROFILE dst_profile = cmsOpenProfileFromMem(
      kHyperGenericCmykIcc,
      static_cast<cmsUInt32Number>(kHyperGenericCmykIccLen));
  if (!src_profile || !dst_profile) {
    if (src_profile) cmsCloseProfile(src_profile);
    if (dst_profile) cmsCloseProfile(dst_profile);
    return false;
  }

  const cmsUInt32Number src_type = (bpp == 24) ? TYPE_BGR_8 : TYPE_BGRA_8;
  cmsHTRANSFORM xform = cmsCreateTransform(
      src_profile, src_type, dst_profile, TYPE_CMYK_8,
      INTENT_PERCEPTUAL, 0);
  cmsCloseProfile(src_profile);
  cmsCloseProfile(dst_profile);
  if (!xform) return false;

  DataVector<uint8_t> cmyk_pixels(static_cast<size_t>(w) * h * 4);
  const int src_stride = src->GetPitch();
  pdfium::span<const uint8_t> src_buf = src->GetBuffer();
  if (src_buf.empty()) {
    cmsDeleteTransform(xform);
    return false;
  }
  for (int y = 0; y < h; ++y) {
    cmsDoTransform(xform, src_buf.data() + y * src_stride,
                   cmyk_pixels.data() + static_cast<size_t>(y) * w * 4,
                   w);
  }
  cmsDeleteTransform(xform);

  DataVector<uint8_t> flate_bytes =
      FlateModule::Encode(pdfium::span<const uint8_t>(
          cmyk_pixels.data(), cmyk_pixels.size()));

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
  dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceCMYK");
  dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");

  return image->OverwriteStreamInPlace(std::move(flate_bytes),
                                        std::move(dict),
                                         false);
}

bool HyperConvertAndEmitGray(CPDF_Document* doc,
                             RetainPtr<CPDF_Image> image,
                             const RetainPtr<CFX_DIBitmap>& gray8,
                             const RetainPtr<const CPDF_Object>& smask_ref,
                             const RetainPtr<const CPDF_Object>& mask_ref) {
  if (!doc || !image || !gray8) return false;
  const int w = gray8->GetWidth();
  const int h = gray8->GetHeight();
  if (w < 1 || h < 1 || gray8->GetBPP() != 8) return false;

  DataVector<uint8_t> gray_pixels(static_cast<size_t>(w) * h);
  const int src_stride = gray8->GetPitch();
  pdfium::span<const uint8_t> src_buf = gray8->GetBuffer();
  if (src_buf.empty() || src_stride < w) return false;
  for (int y = 0; y < h; ++y) {
    std::memcpy(gray_pixels.data() + static_cast<size_t>(y) * w,
                src_buf.data() + static_cast<size_t>(y) * src_stride,
                static_cast<size_t>(w));
  }

  DataVector<uint8_t> flate_bytes =
      FlateModule::Encode(pdfium::span<const uint8_t>(
          gray_pixels.data(), gray_pixels.size()));

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
  dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
  dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");

  if (smask_ref) dict->SetFor("SMask", smask_ref->Clone());
  if (mask_ref) dict->SetFor("Mask", mask_ref->Clone());

  return image->OverwriteStreamInPlace(std::move(flate_bytes),
                                        std::move(dict),
                                         false);
}

extern "C" {
bool HyperJpegliEncode(const uint8_t* src, int width, int height, int stride,
                       int components_in, int is_bgr, int quality,
                       int progressive, int subsample420, uint8_t** out_buf,
                       size_t* out_len);
void HyperJpegliFree(void* p);
}

bool HyperOverwriteJpegInPlace(CPDF_Document* doc,
                               const RetainPtr<CPDF_Image>& image,
                               DataVector<uint8_t> jpeg_bytes) {
  if (!doc || !image || jpeg_bytes.empty()) return false;
  std::optional<JpegModule::ImageInfo> info_opt =
      JpegModule::LoadInfo(jpeg_bytes);
  if (!info_opt.has_value()) return false;
  const JpegModule::ImageInfo& info = info_opt.value();
  const char* csname = nullptr;
  if (info.num_components == 1) {
    csname = "DeviceGray";
  } else if (info.num_components == 3) {
    csname = "DeviceRGB";
  } else if (info.num_components == 4) {
    csname = "DeviceCMYK";
  } else {
    return false;
  }
  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", static_cast<int>(info.width));
  dict->SetNewFor<CPDF_Number>("Height", static_cast<int>(info.height));
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", static_cast<int>(info.bits_per_components));
  dict->SetNewFor<CPDF_Name>("ColorSpace", csname);
  dict->SetNewFor<CPDF_Name>("Filter", "DCTDecode");
  if (info.num_components == 4) {

    auto pDecode = dict->SetNewFor<CPDF_Array>("Decode");
    for (int n = 0; n < 4; ++n) {
      pDecode->AppendNew<CPDF_Number>(1);
      pDecode->AppendNew<CPDF_Number>(0);
    }
  }
  if (!info.color_transform) {
    auto pParms = dict->SetNewFor<CPDF_Dictionary>("DecodeParms");
    pParms->SetNewFor<CPDF_Number>("ColorTransform", 0);
  }
  return image->OverwriteStreamInPlace(std::move(jpeg_bytes), std::move(dict),
                                         false);
}

bool HyperEmitJpeg(CPDF_Document* doc,
                   RetainPtr<CPDF_Image> image,
                   const RetainPtr<CFX_DIBitmap>& bitmap,
                   int quality,
                   int subsample,
                   bool optimize_huffman,
                   bool progressive,
                   size_t skip_if_ge = 0) {
  if (!image || !bitmap) return false;

  const int bpp = bitmap->GetBPP();
  if (!getenv("HYPER_JPEGLI_OFF") && (bpp == 8 || bpp == 24 || bpp == 32)) {
    pdfium::span<const uint8_t> buf = bitmap->GetBuffer();
    if (!buf.empty() && bitmap->GetPitch() > 0) {
      const int comp = bpp / 8;
      uint8_t* jl_buf = nullptr;
      size_t jl_len = 0;
      const bool jl_ok = HyperJpegliEncode(
          buf.data(), bitmap->GetWidth(), bitmap->GetHeight(),
          bitmap->GetPitch(), comp,  comp >= 3 ? 1 : 0, quality,
          progressive ? 1 : 0, subsample ? 1 : 0, &jl_buf, &jl_len);
      if (jl_ok && jl_buf && jl_len) {
        if (getenv("HYPER_JPEGLI_DEBUG")) {
          fprintf(stderr, "[jpegli] %dx%d comp=%d q=%d -> %zu bytes\n",
                  bitmap->GetWidth(), bitmap->GetHeight(), comp, quality,
                  jl_len);
        }

        if (skip_if_ge && jl_len >= skip_if_ge) {
          HyperJpegliFree(jl_buf);
          return false;
        }
        DataVector<uint8_t> jpeg_bytes(jl_buf, jl_buf + jl_len);
        HyperJpegliFree(jl_buf);
        return HyperOverwriteJpegInPlace(doc, image, std::move(jpeg_bytes));
      }
      if (jl_buf) HyperJpegliFree(jl_buf);
      if (getenv("HYPER_JPEGLI_DEBUG")) {
        fprintf(stderr, "[jpegli] declined %dx%d comp=%d -> stock encoder\n",
                bitmap->GetWidth(), bitmap->GetHeight(), comp);
      }

    }
  }

  uint8_t* dest_buf = nullptr;
  size_t dest_size = 0;
  bool ok = false;
  UNSAFE_BUFFERS({
    ok = JpegModule::JpegEncode(bitmap, &dest_buf, &dest_size, quality,
                                 subsample, optimize_huffman, progressive);
  });
  if (!ok || dest_buf == nullptr || dest_size == 0) {
    if (dest_buf != nullptr) FX_Free(dest_buf);
    return false;
  }
  if (skip_if_ge && dest_size >= skip_if_ge) {
    FX_Free(dest_buf);
    return false;
  }
  DataVector<uint8_t> jpeg_bytes(dest_buf,
                                  UNSAFE_TODO(dest_buf + dest_size));
  FX_Free(dest_buf);
  return HyperOverwriteJpegInPlace(doc, image, std::move(jpeg_bytes));
}

bool HyperEmitMonochromeCCITT(CPDF_Document* doc,
                               RetainPtr<CPDF_Image> image,
                               const RetainPtr<CFX_DIBitmap>& bitmap) {
  if (!doc || !image || !bitmap || bitmap->GetBPP() != 1) return false;
  DataVector<uint8_t> g4_bytes = FaxModule::FaxEncode(bitmap);
  if (g4_bytes.empty()) return false;

  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
  dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
  dict->SetNewFor<CPDF_Name>("Filter", "CCITTFaxDecode");

  auto parms = dict->SetNewFor<CPDF_Dictionary>("DecodeParms");
  parms->SetNewFor<CPDF_Number>("K", -1);
  parms->SetNewFor<CPDF_Number>("Columns", w);
  parms->SetNewFor<CPDF_Number>("Rows", h);

  RetainPtr<CPDF_Stream> new_stream =
      pdfium::MakeRetain<CPDF_Stream>(std::move(g4_bytes), std::move(dict));
  RetainPtr<const CPDF_Stream> orig_stream = image->GetStream();
  uint32_t orig_objnum = orig_stream ? orig_stream->GetObjNum() : 0;
  if (orig_objnum == 0) return false;
  doc->ReplaceIndirectObjectIfHigherGeneration(orig_objnum,
                                                std::move(new_stream));
  return true;
}

extern "C" {
bool HyperJbig2EncodeMono(const uint8_t* src_data,
                          int src_stride,
                          int width,
                          int height,
                          int xres,
                          int yres,
                          uint8_t** out_buf,
                          size_t* out_len);
void HyperJbig2Free(void* buf);

}

bool HyperJbig2NeedsDecodeFlip(const RetainPtr<CFX_DIBitmap>& bitmap) {
  if (!bitmap)
    return false;
  const uint32_t c0 = bitmap->GetPaletteArgb(0);
  const int lum = ((c0 >> 16 & 0xff) * 30 + (c0 >> 8 & 0xff) * 59 +
                   (c0 & 0xff) * 11) / 100;
  return lum < 128;
}

void HyperJbig2MaybeAddDecodeFlip(CPDF_Dictionary* dict,
                                  const RetainPtr<CFX_DIBitmap>& bitmap) {
  if (!dict || !HyperJbig2NeedsDecodeFlip(bitmap))
    return;
  auto dec = dict->SetNewFor<CPDF_Array>("Decode");
  dec->AppendNew<CPDF_Number>(1);
  dec->AppendNew<CPDF_Number>(0);
}

bool HyperEmitJbig2(CPDF_Document* doc,
                    RetainPtr<CPDF_Image> image,
                    const RetainPtr<CFX_DIBitmap>& bitmap) {
  if (!doc || !image || !bitmap || bitmap->GetBPP() != 1) return false;
  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  if (w < 1 || h < 1) return false;
  pdfium::span<const uint8_t> src_buf = bitmap->GetBuffer();
  const int src_stride = bitmap->GetPitch();
  if (src_buf.empty() || src_stride <= 0) return false;

  uint8_t* jbig2_buf = nullptr;
  size_t jbig2_len = 0;
  if (!HyperJbig2EncodeMono(src_buf.data(), src_stride, w, h, 300, 300,
                             &jbig2_buf, &jbig2_len) ||
      !jbig2_buf || jbig2_len == 0) {
    if (jbig2_buf) HyperJbig2Free(jbig2_buf);
    return false;
  }
  DataVector<uint8_t> jbig2_bytes(jbig2_buf, jbig2_buf + jbig2_len);
  HyperJbig2Free(jbig2_buf);

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
  dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
  dict->SetNewFor<CPDF_Name>("Filter", "JBIG2Decode");
  HyperJbig2MaybeAddDecodeFlip(dict.Get(), bitmap);

  RetainPtr<CPDF_Stream> new_stream = pdfium::MakeRetain<CPDF_Stream>(
      std::move(jbig2_bytes), std::move(dict));
  RetainPtr<const CPDF_Stream> orig_stream = image->GetStream();
  uint32_t orig_objnum = orig_stream ? orig_stream->GetObjNum() : 0;
  if (orig_objnum == 0) return false;
  doc->ReplaceIndirectObjectIfHigherGeneration(orig_objnum,
                                                std::move(new_stream));
  return true;
}

bool HyperEmitJbig2Stencil(CPDF_Document* doc,
                           RetainPtr<CPDF_Image> image,
                           const RetainPtr<CFX_DIBBase>& src) {
  if (!doc || !image || !src || src->GetBPP() != 1) return false;

  RetainPtr<CFX_DIBitmap> bitmap = src->Realize();
  if (!bitmap || bitmap->GetBPP() != 1) return false;
  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  if (w < 1 || h < 1) return false;
  pdfium::span<const uint8_t> buf = bitmap->GetBuffer();
  const int stride = bitmap->GetPitch();
  if (buf.empty() || stride <= 0) return false;
  RetainPtr<const CPDF_Stream> orig_stream = image->GetStream();
  if (!orig_stream) return false;

  uint8_t* jbig2_buf = nullptr;
  size_t jbig2_len = 0;
  if (!HyperJbig2EncodeMono(buf.data(), stride, w, h, 300, 300, &jbig2_buf,
                             &jbig2_len) ||
      !jbig2_buf || jbig2_len == 0) {
    if (jbig2_buf) HyperJbig2Free(jbig2_buf);
    return false;
  }
  if (jbig2_len >= orig_stream->GetRawSize()) {
    HyperJbig2Free(jbig2_buf);
    return false;
  }
  DataVector<uint8_t> jbig2_bytes(jbig2_buf, jbig2_buf + jbig2_len);
  HyperJbig2Free(jbig2_buf);

  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
  dict->SetNewFor<CPDF_Boolean>("ImageMask", true);
  dict->SetNewFor<CPDF_Name>("Filter", "JBIG2Decode");

  return image->OverwriteStreamInPlace(std::move(jbig2_bytes), std::move(dict),
                                        false);
}

namespace {
struct Jp2WriteCtx {
  std::vector<uint8_t> buf;
  size_t pos = 0;
};
OPJ_SIZE_T HyperJp2WriteCb(void* p_buffer, OPJ_SIZE_T n, void* p_user) {
  auto* ctx = static_cast<Jp2WriteCtx*>(p_user);
  if (ctx->pos + n > ctx->buf.size()) ctx->buf.resize(ctx->pos + n);
  std::memcpy(ctx->buf.data() + ctx->pos,
              static_cast<const uint8_t*>(p_buffer), n);
  ctx->pos += n;
  return n;
}
OPJ_OFF_T HyperJp2SkipCb(OPJ_OFF_T n, void* p_user) {
  auto* ctx = static_cast<Jp2WriteCtx*>(p_user);
  if (n < 0) return -1;
  ctx->pos += static_cast<size_t>(n);
  if (ctx->pos > ctx->buf.size()) ctx->buf.resize(ctx->pos);
  return n;
}
OPJ_BOOL HyperJp2SeekCb(OPJ_OFF_T offset, void* p_user) {
  auto* ctx = static_cast<Jp2WriteCtx*>(p_user);
  if (offset < 0) return OPJ_FALSE;
  ctx->pos = static_cast<size_t>(offset);
  if (ctx->pos > ctx->buf.size()) ctx->buf.resize(ctx->pos);
  return OPJ_TRUE;
}
}

bool HyperEmitJpeg2000(CPDF_Document* doc,
                       RetainPtr<CPDF_Image> image,
                       const RetainPtr<CFX_DIBitmap>& bitmap,
                       int quality,
                       size_t budget = 0) {
  if (!doc || !image || !bitmap) return false;
  const int w = bitmap->GetWidth();
  const int h = bitmap->GetHeight();
  const int bpp = bitmap->GetBPP();
  if (w < 1 || h < 1) return false;

  if (bpp != 8 && bpp != 24 && bpp != 32) return false;

  constexpr int kMinJpxPixels = 128 * 128;
  if (static_cast<int64_t>(w) * h < kMinJpxPixels) return false;

  opj_image_cmptparm_t comptparms[3] = {};
  for (int c = 0; c < 3; ++c) {
    comptparms[c].dx = 1;
    comptparms[c].dy = 1;
    comptparms[c].w = static_cast<OPJ_UINT32>(w);
    comptparms[c].h = static_cast<OPJ_UINT32>(h);
    comptparms[c].prec = 8;
    comptparms[c].sgnd = 0;
  }
  opj_image_t* opj_img =
      opj_image_create(3, comptparms, OPJ_CLRSPC_SRGB);
  if (!opj_img) return false;
  opj_img->x1 = static_cast<OPJ_UINT32>(w);
  opj_img->y1 = static_cast<OPJ_UINT32>(h);

  pdfium::span<const uint8_t> src_buf = bitmap->GetBuffer();
  const int stride = bitmap->GetPitch();
  const int step = (bpp == 24) ? 3 : (bpp == 32 ? 4 : 1);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = src_buf.data() + y * stride;
    for (int x = 0; x < w; ++x) {
      const uint8_t* px = row + x * step;
      const int idx = y * w + x;
      if (bpp == 8) {
        const OPJ_INT32 g = static_cast<OPJ_INT32>(px[0]);
        opj_img->comps[0].data[idx] = g;
        opj_img->comps[1].data[idx] = g;
        opj_img->comps[2].data[idx] = g;
      } else {

        opj_img->comps[0].data[idx] = static_cast<OPJ_INT32>(px[2]);
        opj_img->comps[1].data[idx] = static_cast<OPJ_INT32>(px[1]);
        opj_img->comps[2].data[idx] = static_cast<OPJ_INT32>(px[0]);
      }
    }
  }

  opj_cparameters_t params;
  opj_set_default_encoder_parameters(&params);
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;

  const float target_bpp = 0.0115f * static_cast<float>(quality) + 0.04f;
  float rate = 24.0f / target_bpp;
  if (rate < 1.0f) rate = 1.0f;
  if (rate > 300.0f) rate = 300.0f;
  params.tcp_numlayers = 1;
  params.tcp_rates[0] = rate;
  params.cp_disto_alloc = 1;
  params.irreversible = 1;
  params.tcp_mct = 1;

  opj_codec_t* codec = opj_create_compress(OPJ_CODEC_JP2);
  if (!codec) {
    opj_image_destroy(opj_img);
    return false;
  }
  if (!opj_setup_encoder(codec, &params, opj_img)) {
    opj_destroy_codec(codec);
    opj_image_destroy(opj_img);
    return false;
  }

  Jp2WriteCtx ctx;
  ctx.buf.reserve(static_cast<size_t>(w) * h);
  opj_stream_t* stream =
      opj_stream_create(1024 * 1024, OPJ_FALSE);
  if (!stream) {
    opj_destroy_codec(codec);
    opj_image_destroy(opj_img);
    return false;
  }
  opj_stream_set_write_function(stream, &HyperJp2WriteCb);
  opj_stream_set_skip_function(stream, &HyperJp2SkipCb);
  opj_stream_set_seek_function(stream, &HyperJp2SeekCb);
  opj_stream_set_user_data(stream, &ctx, nullptr);
  opj_stream_set_user_data_length(stream, 0);

  OPJ_BOOL ok = opj_start_compress(codec, opj_img, stream);
  if (ok) ok = opj_encode(codec, stream);
  if (ok) ok = opj_end_compress(codec, stream);

  opj_stream_destroy(stream);
  opj_destroy_codec(codec);
  opj_image_destroy(opj_img);

  if (!ok || ctx.buf.empty()) return false;

  if (budget > 0 && ctx.buf.size() >= budget) return false;

  {
    constexpr double kMaxJpxMeanAbsErr = 12.0;
    auto jdict = doc->New<CPDF_Dictionary>();
    jdict->SetNewFor<CPDF_Name>("Type", "XObject");
    jdict->SetNewFor<CPDF_Name>("Subtype", "Image");
    jdict->SetNewFor<CPDF_Number>("Width", w);
    jdict->SetNewFor<CPDF_Number>("Height", h);
    jdict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

    jdict->SetNewFor<CPDF_Name>("Filter", "JPXDecode");
    auto jstream = pdfium::MakeRetain<CPDF_Stream>(
        DataVector<uint8_t>(ctx.buf.begin(), ctx.buf.end()),
        std::move(jdict));
    auto jimg = pdfium::MakeRetain<CPDF_Image>(doc, std::move(jstream));
    RetainPtr<CFX_DIBBase> dec = jimg->LoadDIBBase();
    if (!dec || dec->GetWidth() != w || dec->GetHeight() != h ||
        dec->GetBPP() < 24) {
      return false;
    }
    const int dcomps = dec->GetBPP() / 8;
    double err_sum = 0;
    size_t cnt = 0;
    for (int y = 0; y < h; ++y) {
      pdfium::span<const uint8_t> drow = dec->GetScanline(y);
      const uint8_t* srow = src_buf.data() + static_cast<size_t>(y) * stride;
      if (drow.size() < static_cast<size_t>(w) * dcomps) {
        cnt = 0;
        break;
      }
      for (int x = 0; x < w; ++x) {
        const uint8_t* dp = drow.data() + static_cast<size_t>(x) * dcomps;
        if (bpp == 8) {
          const int g = srow[x];
          err_sum += (std::abs(int(dp[0]) - g) + std::abs(int(dp[1]) - g) +
                      std::abs(int(dp[2]) - g)) /
                     3.0;
        } else {
          const uint8_t* sp = srow + static_cast<size_t>(x) * step;

          err_sum += (std::abs(int(dp[0]) - int(sp[0])) +
                      std::abs(int(dp[1]) - int(sp[1])) +
                      std::abs(int(dp[2]) - int(sp[2]))) /
                     3.0;
        }
        ++cnt;
      }
    }
    if (!cnt || err_sum / cnt > kMaxJpxMeanAbsErr) return false;
  }

  DataVector<uint8_t> jpx_bytes(ctx.buf.data(),
                                 ctx.buf.data() + ctx.buf.size());
  auto dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", w);
  dict->SetNewFor<CPDF_Number>("Height", h);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
  dict->SetNewFor<CPDF_Name>("Filter", "JPXDecode");

  return image->OverwriteStreamInPlace(std::move(jpx_bytes),
                                        std::move(dict),
                                         false);
}

void HyperUnshareResourceTree(CPDF_Document* doc,
                              CPDF_PageObjectHolder* holder) {
  if (!doc || !holder)
    return;
  RetainPtr<CPDF_Dictionary> res = holder->GetMutableResources();
  if (!res)
    return;
  RetainPtr<CPDF_Dictionary> res_clone =
      pdfium::WrapRetain(res->Clone()->AsMutableDictionary());
  static const char* const kResourceClasses[] = {
      "ExtGState", "Font", "XObject", "ColorSpace", "Pattern", "Shading"};
  for (const char* cls : kResourceClasses) {
    RetainPtr<const CPDF_Dictionary> sub = res_clone->GetDictFor(cls);
    if (!sub)
      continue;
    RetainPtr<CPDF_Dictionary> sub_clone =
        pdfium::WrapRetain(sub->Clone()->AsMutableDictionary());
    const uint32_t sub_objnum = doc->AddIndirectObject(std::move(sub_clone));
    res_clone->SetNewFor<CPDF_Reference>(cls, doc, sub_objnum);
  }
  const uint32_t res_objnum = doc->AddIndirectObject(res_clone);
  holder->SetResources(res_clone);
  if (RetainPtr<CPDF_Dictionary> hd = holder->GetMutableDict())
    hd->SetNewFor<CPDF_Reference>("Resources", doc, res_objnum);
}

bool HyperImageSourceIsLossy(const RetainPtr<CPDF_Image>& image) {
  RetainPtr<const CPDF_Stream> s = image ? image->GetStream() : nullptr;
  RetainPtr<const CPDF_Dictionary> d = s ? s->GetDict() : nullptr;
  if (!d)
    return false;
  auto is_lossy_name = [](const ByteString& n) {
    return n == "DCTDecode" || n == "JPXDecode";
  };
  RetainPtr<const CPDF_Object> f = d->GetDirectObjectFor("Filter");
  if (!f)
    return false;
  if (f->IsName())
    return is_lossy_name(f->GetString());
  if (const CPDF_Array* arr = f->AsArray()) {
    for (size_t i = 0; i < arr->size(); ++i) {
      RetainPtr<const CPDF_Object> e = arr->GetDirectObjectAt(i);
      if (e && e->IsName() && is_lossy_name(e->GetString()))
        return true;
    }
  }
  return false;
}

bool HyperImageSourceIsRawRecompressible(const RetainPtr<CPDF_Image>& image) {
  RetainPtr<const CPDF_Stream> s = image ? image->GetStream() : nullptr;
  RetainPtr<const CPDF_Dictionary> d = s ? s->GetDict() : nullptr;
  if (!d)
    return false;

  if (d->GetIntegerFor("BitsPerComponent", 0) != 8)
    return false;
  if (d->GetBooleanFor("ImageMask", false))
    return false;
  RetainPtr<const CPDF_Object> cs = d->GetDirectObjectFor("ColorSpace");
  if (!cs)
    return false;
  if (cs->IsName()) {
    const ByteString n = cs->GetString();
    return n == "DeviceRGB" || n == "DeviceGray" ||
           n == "CalRGB" || n == "CalGray";
  }
  if (const CPDF_Array* a = cs->AsArray()) {
    RetainPtr<const CPDF_Object> head = a->GetObjectAt(0);
    const CPDF_Name* hn = head ? head->AsName() : nullptr;
    if (!hn)
      return false;
    const ByteString fam = hn->GetString();
    if (fam == "CalRGB" || fam == "CalGray")
      return true;
    if (fam == "ICCBased") {
      RetainPtr<const CPDF_Object> e1 = a->GetDirectObjectAt(1);
      const CPDF_Stream* icc = e1 ? e1->AsStream() : nullptr;
      RetainPtr<const CPDF_Dictionary> icd = icc ? icc->GetDict() : nullptr;
      const int ncomp = icd ? icd->GetIntegerFor("N", 0) : 0;
      return ncomp == 1 || ncomp == 3;
    }
  }
  return false;
}

inline bool HyperBitmapIsLineArt(const CFX_DIBBase* bm) {
  if (!bm || bm->GetBPP() < 24)
    return false;
  const int w = bm->GetWidth(), h = bm->GetHeight();
  if (w < 2 || h < 2)
    return false;
  const int comps = bm->GetBPP() / 8;
  const int64_t total = static_cast<int64_t>(w) * h;
  const int step = total > 200000 ? static_cast<int>(total / 200000) : 1;
  int64_t n = 0, extreme = 0, midtone = 0, idx = 0;
  for (int y = 0; y < h; ++y) {
    pdfium::span<const uint8_t> row = bm->GetScanline(y);
    if (row.size() < static_cast<size_t>(w) * comps)
      continue;
    const uint8_t* p = row.data();
    for (int x = 0; x < w; ++x) {
      if (idx++ % step)
        continue;
      const uint8_t* px = p + static_cast<size_t>(x) * comps;
      const int lum = (px[2] * 54 + px[1] * 183 + px[0] * 19) >> 8;
      ++n;
      if (lum < 40 || lum > 215)
        ++extreme;
      else if (lum >= 80 && lum <= 175)
        ++midtone;
    }
  }
  if (n == 0)
    return false;
  const double extreme_frac = static_cast<double>(extreme) / n;
  const double midtone_frac = static_cast<double>(midtone) / n;
  return midtone_frac < 0.20 && extreme_frac > 0.55;
}

inline double HyperBitmapEdgeDensity(const CFX_DIBBase* bm) {
  if (!bm || bm->GetBPP() < 24)
    return 1.0;
  const int w = bm->GetWidth(), h = bm->GetHeight();
  if (w < 2 || h < 2)
    return 1.0;
  const int comps = bm->GetBPP() / 8;
  const int64_t total = static_cast<int64_t>(w) * h;
  const int step = total > 200000 ? static_cast<int>(total / 200000) : 1;
  auto lum = [](const uint8_t* px) -> int {
    return (px[2] * 54 + px[1] * 183 + px[0] * 19) >> 8;
  };
  int64_t n = 0, sharp = 0, idx = 0;
  for (int y = 0; y < h; ++y) {
    pdfium::span<const uint8_t> row = bm->GetScanline(y);
    if (row.size() < static_cast<size_t>(w) * comps)
      continue;
    const uint8_t* p = row.data();
    const bool have_below = (y + 1 < h);
    pdfium::span<const uint8_t> row2 =
        have_below ? bm->GetScanline(y + 1) : row;
    const bool below_ok =
        have_below && row2.size() >= static_cast<size_t>(w) * comps;
    const uint8_t* p2 = row2.data();
    for (int x = 0; x + 1 < w; ++x) {
      if (idx++ % step)
        continue;
      const uint8_t* a = p + static_cast<size_t>(x) * comps;
      const int la = lum(a);
      ++n;
      if (std::abs(la - lum(a + comps)) > 76)
        ++sharp;
      if (below_ok) {
        ++n;
        if (std::abs(la - lum(p2 + static_cast<size_t>(x) * comps)) > 76)
          ++sharp;
      }
    }
  }
  if (n == 0)
    return 1.0;
  return static_cast<double>(sharp) / n;
}

inline bool HyperIndexedIsPhotographic(const CFX_DIBBase* realised_rgb) {
  return HyperBitmapEdgeDensity(realised_rgb) < 0.030;
}

inline bool HyperBitmapIsAchromatic(const CFX_DIBBase* bm) {
  if (!bm || bm->GetBPP() < 24)
    return false;
  const int w = bm->GetWidth(), h = bm->GetHeight();
  if (w < 2 || h < 2)
    return false;
  const int comps = bm->GetBPP() / 8;
  const int64_t total = static_cast<int64_t>(w) * h;
  const int step = total > 200000 ? static_cast<int>(total / 200000) : 1;
  int64_t n = 0, colored = 0, idx = 0;
  for (int y = 0; y < h; ++y) {
    pdfium::span<const uint8_t> row = bm->GetScanline(y);
    if (row.size() < static_cast<size_t>(w) * comps)
      continue;
    const uint8_t* p = row.data();
    for (int x = 0; x < w; ++x) {
      if (idx++ % step)
        continue;
      const uint8_t* px = p + static_cast<size_t>(x) * comps;
      const int b = px[0], g = px[1], r = px[2];
      const int mx = std::max(std::max(r, g), b);
      const int mn = std::min(std::min(r, g), b);
      ++n;
      if (mx - mn > 16)
        ++colored;
    }
  }
  if (n == 0)
    return false;
  return static_cast<double>(colored) / n <= 0.02;
}

static std::vector<uint8_t> HyperRenderPreviewGray(CPDF_Document* doc,
                                                   int page_index,
                                                   int* out_w,
                                                   int* out_h) {
  *out_w = 0;
  *out_h = 0;
  FPDF_DOCUMENT fdoc = FPDFDocumentFromCPDFDocument(doc);
  FPDF_PAGE fpage = FPDF_LoadPage(fdoc, page_index);
  if (!fpage)
    return {};
  const double wpts = FPDF_GetPageWidthF(fpage);
  const double hpts = FPDF_GetPageHeightF(fpage);
  if (wpts <= 0 || hpts <= 0) {
    FPDF_ClosePage(fpage);
    return {};
  }
  const double scale = 640.0 / std::max(wpts, hpts);
  const int w = std::max(1, static_cast<int>(wpts * std::min(1.0, scale)));
  const int h = std::max(1, static_cast<int>(hpts * std::min(1.0, scale)));
  FPDF_BITMAP bmp = FPDFBitmap_Create(w, h, 0);
  if (!bmp) {
    FPDF_ClosePage(fpage);
    return {};
  }
  FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
  FPDF_RenderPageBitmap(bmp, fpage, 0, 0, w, h, 0, 0);
  const uint8_t* buf = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bmp));
  const int stride = FPDFBitmap_GetStride(bmp);
  std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
  if (buf && stride > 0) {
    for (int y = 0; y < h; ++y) {
      const uint8_t* row = buf + static_cast<size_t>(y) * stride;
      for (int x = 0; x < w; ++x) {
        const uint8_t* px = row + static_cast<size_t>(x) * 4;
        gray[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(
            (px[2] * 54 + px[1] * 183 + px[0] * 19) >> 8);
      }
    }
  }
  FPDFBitmap_Destroy(bmp);
  FPDF_ClosePage(fpage);
  *out_w = w;
  *out_h = h;
  return gray;
}

static double HyperPreviewMeanAbsDiff(const std::vector<uint8_t>& a, int aw,
                                      int ah, const std::vector<uint8_t>& b,
                                      int bw, int bh) {
  if (a.empty() || b.empty() || aw != bw || ah != bh)
    return 255.0;
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i)
    sum += std::abs(int(a[i]) - int(b[i]));
  return sum / static_cast<double>(a.size());
}

struct HyperRegenSnapshot {
  struct StreamState {
    uint32_t objnum = 0;
    DataVector<uint8_t> raw;
    RetainPtr<CPDF_Dictionary> dict_clone;
  };
  RetainPtr<CPDF_Dictionary> page_dict;
  RetainPtr<CPDF_Object> contents_value;
  RetainPtr<CPDF_Object> resources_value;
  std::vector<StreamState> streams;
};

static void HyperSnapshotStream(const CPDF_Stream* st,
                    std::vector<HyperRegenSnapshot::StreamState>* out) {
  if (!st || st->GetObjNum() == 0)
    return;
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(st));
  acc->LoadAllDataRaw();
  pdfium::span<const uint8_t> sp = acc->GetSpan();
  HyperRegenSnapshot::StreamState ss;
  ss.objnum = st->GetObjNum();
  ss.raw = DataVector<uint8_t>(sp.begin(), sp.end());
  RetainPtr<const CPDF_Dictionary> d = st->GetDict();
  ss.dict_clone = d ? ToDictionary(d->Clone()) : nullptr;
  out->push_back(std::move(ss));
}

static HyperRegenSnapshot HyperCaptureRegenSnapshot(
    const RetainPtr<CPDF_Dictionary>& page_dict,
    const std::vector<CPDF_Form*>& forms) {
  HyperRegenSnapshot snap;
  snap.page_dict = page_dict;
  RetainPtr<const CPDF_Object> cv = page_dict->GetObjectFor("Contents");
  snap.contents_value = cv ? cv->Clone() : nullptr;
  RetainPtr<const CPDF_Object> rv = page_dict->GetObjectFor("Resources");
  snap.resources_value = rv ? rv->Clone() : nullptr;

  RetainPtr<const CPDF_Object> cdir =
      page_dict->GetDirectObjectFor("Contents");
  if (const CPDF_Stream* cs = cdir ? cdir->AsStream() : nullptr) {
    HyperSnapshotStream(cs, &snap.streams);
  } else if (const CPDF_Array* ca = cdir ? cdir->AsArray() : nullptr) {
    for (size_t i = 0; i < ca->size(); ++i)
      HyperSnapshotStream(ca->GetStreamAt(i).Get(), &snap.streams);
  }

  for (const CPDF_Form* f : forms) {
    if (f)
      HyperSnapshotStream(f->GetStream().Get(), &snap.streams);
  }
  return snap;
}

static void HyperRestoreRegenSnapshot(CPDF_Document* doc,
                                      const HyperRegenSnapshot& snap) {
  if (!snap.page_dict)
    return;
  if (snap.contents_value)
    snap.page_dict->SetFor("Contents", snap.contents_value->Clone());
  else
    snap.page_dict->RemoveFor("Contents");
  if (snap.resources_value)
    snap.page_dict->SetFor("Resources", snap.resources_value->Clone());
  else
    snap.page_dict->RemoveFor("Resources");
  for (const auto& ss : snap.streams) {
    RetainPtr<CPDF_Object> obj = doc->GetOrParseIndirectObject(ss.objnum);
    CPDF_Stream* st = obj ? obj->AsMutableStream() : nullptr;
    if (!st)
      continue;

    st->TakeData(DataVector<uint8_t>(ss.raw));
    RetainPtr<CPDF_Dictionary> dict = st->GetMutableDict();
    if (dict && ss.dict_clone) {
      std::vector<ByteString> keys;
      {
        CPDF_DictionaryLocker locker(dict.Get());
        for (const auto& it : locker)
          keys.push_back(it.first);
      }
      for (const ByteString& k : keys) {
        if (k != "Length")
          dict->RemoveFor(k.AsStringView());
      }
      CPDF_DictionaryLocker locker(ss.dict_clone.Get());
      for (const auto& it : locker) {
        if (it.first != "Length" && it.second)
          dict->SetFor(it.first, it.second->Clone());
      }
    }
  }
}

class HyperImageRewrite {
 public:
  HyperImageRewrite(CPDF_Document* doc, const CompressOptions& opts,
                    const std::unordered_set<int>& skip_pages)
      : doc_(doc), opts_(opts), skip_pages_(skip_pages) {}

  int Run() {
    int rewrites = 0;

    BuildImageRefCensus();

    std::function<bool(CPDF_PageObjectHolder*, const CFX_Matrix&)> walk_holder =
        [&](CPDF_PageObjectHolder* holder, const CFX_Matrix& holder_ctm)
            -> bool {
          if (!holder) return false;
          bool holder_changed = false;
          const size_t obj_count = holder->GetActivePageObjectCount();

          size_t holder_image_objs = 0;
          for (size_t i = 0; i < obj_count; ++i) {
            CPDF_PageObject* o = holder->GetPageObjectByIndex(i);
            if (o && o->IsImage())
              ++holder_image_objs;
          }
          const bool skip_image_rewrite =
              holder_image_objs > kMaxImagesToRewrite;
          for (size_t i = 0; i < obj_count; ++i) {
            CPDF_PageObject* obj = holder->GetPageObjectByIndex(i);
            if (!obj) continue;
            if (obj->IsImage()) {
              if (skip_image_rewrite) continue;
              CPDF_ImageObject* image_obj = obj->AsImage();
              if (image_obj &&
                  MaybeRewriteImageObject(image_obj, holder_ctm)) {

                if (last_rewrite_needs_regen_) holder_changed = true;
                ++rewrites;
              }
            } else if (obj->IsForm()) {
              CPDF_FormObject* form_obj = obj->AsForm();
              if (form_obj && form_obj->form()) {

                if (walk_holder(form_obj->form(),
                                form_obj->form_matrix() * holder_ctm)) {

                  pending_form_regens_.push_back(form_obj->form());
                }
              }
            }
          }
          return holder_changed;
        };
    const int page_count = doc_->GetPageCount();
    for (int page_index = 0; page_index < page_count; ++page_index) {
      if (skip_pages_.count(page_index)) continue;
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(page_index);
      if (!page_dict) continue;
      if (!HyperIsLoadablePageDict(page_dict.Get())) continue;
      auto page = pdfium::MakeRetain<CPDF_Page>(doc_, page_dict);
      page->ParseContent();

      pending_form_regens_.clear();
      const bool page_changed = walk_holder(page.Get(), CFX_Matrix());
      if (page_changed || !pending_form_regens_.empty()) {

        int pw = 0, ph = 0, qw = 0, qh = 0;
        std::vector<uint8_t> pre =
            HyperRenderPreviewGray(doc_, page_index, &pw, &ph);
        HyperRegenSnapshot snap =
            HyperCaptureRegenSnapshot(page_dict, pending_form_regens_);
        for (CPDF_Form* f : pending_form_regens_) {
          HyperUnshareResourceTree(doc_, f);
          CPDF_PageContentGenerator nested_gen(f);
          nested_gen.GenerateContent();
        }
        if (page_changed) {
          HyperUnshareResourceTree(doc_, page.Get());
          CPDF_PageContentGenerator generator(page.Get());
          generator.GenerateContent();
        }
        std::vector<uint8_t> post =
            HyperRenderPreviewGray(doc_, page_index, &qw, &qh);
        const double mad = HyperPreviewMeanAbsDiff(pre, pw, ph, post, qw, qh);
        if (mad > 2.5) {
          HyperRestoreRegenSnapshot(doc_, snap);
          fprintf(stderr,
                  "[hyper] regen verify FAILED on page %d (meanAbsDiff %.1f)"
                  "  - content restored, images kept in place\n",
                  page_index + 1, mad);
        }
      }
    }

    FinaliseJbig2Batch();
    return rewrites;
  }

 private:

  bool HyperEmitSpotResampled(RetainPtr<CPDF_Image> image,
                              RetainPtr<const CPDF_Dictionary> img_dict,
                              int src_w, int src_h, int tw, int th, int n) {
    if (!image || !img_dict || n < 1 || n > 32 || tw < 1 || th < 1 ||
        src_w < 1 || src_h < 1)
      return false;
    if (img_dict->GetIntegerFor("BitsPerComponent") != 8)
      return false;
    RetainPtr<const CPDF_Stream> stream = image->GetStream();
    if (!stream)
      return false;
    auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(stream);
    acc->LoadAllDataFiltered();
    pdfium::span<const uint8_t> src = acc->GetSpan();
    const size_t need = static_cast<size_t>(src_w) * src_h * n;
    if (src.size() < need)
      return false;
    std::vector<uint8_t> dst(static_cast<size_t>(tw) * th * n);
    for (int ty = 0; ty < th; ++ty) {
      int sy0 = static_cast<int>(static_cast<int64_t>(ty) * src_h / th);
      int sy1 = static_cast<int>(static_cast<int64_t>(ty + 1) * src_h / th);
      if (sy1 <= sy0) sy1 = sy0 + 1;
      if (sy1 > src_h) sy1 = src_h;
      for (int tx = 0; tx < tw; ++tx) {
        int sx0 = static_cast<int>(static_cast<int64_t>(tx) * src_w / tw);
        int sx1 = static_cast<int>(static_cast<int64_t>(tx + 1) * src_w / tw);
        if (sx1 <= sx0) sx1 = sx0 + 1;
        if (sx1 > src_w) sx1 = src_w;
        for (int c = 0; c < n; ++c) {
          uint32_t sum = 0, cnt = 0;
          for (int sy = sy0; sy < sy1; ++sy)
            for (int sx = sx0; sx < sx1; ++sx) {
              sum += src[(static_cast<size_t>(sy) * src_w + sx) * n + c];
              ++cnt;
            }
          dst[(static_cast<size_t>(ty) * tw + tx) * n + c] =
              cnt ? static_cast<uint8_t>(sum / cnt) : 0;
        }
      }
    }
    DataVector<uint8_t> flate = FlateModule::Encode(
        pdfium::span<const uint8_t>(dst.data(), dst.size()));
    if (flate.empty() || flate.size() >= stream->GetRawSize())
      return false;
    auto dict = doc_->New<CPDF_Dictionary>();
    dict->SetNewFor<CPDF_Name>("Type", "XObject");
    dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    dict->SetNewFor<CPDF_Number>("Width", tw);
    dict->SetNewFor<CPDF_Number>("Height", th);
    dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
    if (RetainPtr<const CPDF_Object> cs =
            img_dict->GetDirectObjectFor("ColorSpace"))
      dict->SetFor("ColorSpace", cs->Clone());
    if (RetainPtr<const CPDF_Object> dec = img_dict->GetDirectObjectFor("Decode"))
      dict->SetFor("Decode", dec->Clone());
    dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    return image->OverwriteStreamInPlace(std::move(flate), std::move(dict),
                                           false);
  }

  bool HyperEmitLosslessFlate(RetainPtr<CPDF_Image> image,
                              const RetainPtr<CFX_DIBitmap>& bm, bool gray) {
    if (!image || !bm)
      return false;
    RetainPtr<const CPDF_Stream> stream = image->GetStream();
    if (!stream)
      return false;
    const int w = bm->GetWidth(), h = bm->GetHeight();
    if (w < 1 || h < 1)
      return false;
    const int comps = bm->GetBPP() / 8;
    if (comps < 3)
      return false;
    const int n = gray ? 1 : 3;
    std::vector<uint8_t> dst(static_cast<size_t>(w) * h * n);
    size_t o = 0;
    for (int y = 0; y < h; ++y) {
      pdfium::span<const uint8_t> row = bm->GetScanline(y);
      if (row.size() < static_cast<size_t>(w) * comps)
        return false;
      const uint8_t* p = row.data();
      for (int x = 0; x < w; ++x) {
        const uint8_t* px = p + static_cast<size_t>(x) * comps;
        if (gray) {
          dst[o++] = static_cast<uint8_t>(
              (px[2] * 54 + px[1] * 183 + px[0] * 19) >> 8);
        } else {
          dst[o++] = px[2];
          dst[o++] = px[1];
          dst[o++] = px[0];
        }
      }
    }
    DataVector<uint8_t> flate = FlateModule::Encode(
        pdfium::span<const uint8_t>(dst.data(), dst.size()));
    if (flate.empty() || flate.size() >= stream->GetRawSize())
      return false;
    auto dict = doc_->New<CPDF_Dictionary>();
    dict->SetNewFor<CPDF_Name>("Type", "XObject");
    dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    dict->SetNewFor<CPDF_Number>("Width", w);
    dict->SetNewFor<CPDF_Number>("Height", h);
    dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
    dict->SetNewFor<CPDF_Name>("ColorSpace", gray ? "DeviceGray" : "DeviceRGB");
    dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    return image->OverwriteStreamInPlace(std::move(flate), std::move(dict),
                                           false);
  }

  bool HyperEmitLineArtCompete(RetainPtr<CPDF_Image> image,
                               const RetainPtr<CFX_DIBitmap>& bm,
                               bool gray) {
    if (!image || !bm)
      return false;
    RetainPtr<const CPDF_Stream> stream = image->GetStream();
    if (!stream)
      return false;
    const int w = bm->GetWidth(), h = bm->GetHeight();
    if (w < 1 || h < 1)
      return false;
    const int comps = bm->GetBPP() / 8;
    if (comps < 3)
      return false;
    const int n = gray ? 1 : 3;

    std::vector<uint8_t> dst(static_cast<size_t>(w) * h * n);
    size_t o = 0;
    for (int y = 0; y < h; ++y) {
      pdfium::span<const uint8_t> row = bm->GetScanline(y);
      if (row.size() < static_cast<size_t>(w) * comps)
        return false;
      const uint8_t* p = row.data();
      for (int x = 0; x < w; ++x) {
        const uint8_t* px = p + static_cast<size_t>(x) * comps;
        if (gray) {
          dst[o++] = static_cast<uint8_t>(
              (px[2] * 54 + px[1] * 183 + px[0] * 19) >> 8);
        } else {
          dst[o++] = px[2];
          dst[o++] = px[1];
          dst[o++] = px[0];
        }
      }
    }
    const size_t raw_budget = stream->GetRawSize();
    DataVector<uint8_t> flate = FlateModule::Encode(
        pdfium::span<const uint8_t>(dst.data(), dst.size()));
    const size_t flate_size = flate.empty() ? SIZE_MAX : flate.size();

    static constexpr double kMaxMeanAbsErr = 10.0;
    uint8_t* jl = nullptr;
    size_t jn = 0;
    if (HyperJpegliEncode(dst.data(), w, h, w * n, n,  0,
                          opts_.image_quality,
                          opts_.jpeg_progressive != 0 ? 1 : 0,
                           0, &jl, &jn) &&
        jl && jn) {
      if (jn < flate_size && jn < raw_budget) {
        DataVector<uint8_t> jpeg(jl, jl + jn);
        HyperJpegliFree(jl);
        jl = nullptr;

        auto jdict = doc_->New<CPDF_Dictionary>();
        jdict->SetNewFor<CPDF_Name>("Type", "XObject");
        jdict->SetNewFor<CPDF_Name>("Subtype", "Image");
        jdict->SetNewFor<CPDF_Number>("Width", w);
        jdict->SetNewFor<CPDF_Number>("Height", h);
        jdict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
        jdict->SetNewFor<CPDF_Name>("ColorSpace",
                                    gray ? "DeviceGray" : "DeviceRGB");
        jdict->SetNewFor<CPDF_Name>("Filter", "DCTDecode");
        auto jstream = pdfium::MakeRetain<CPDF_Stream>(
            DataVector<uint8_t>(jpeg), std::move(jdict));
        auto jimg = pdfium::MakeRetain<CPDF_Image>(doc_, std::move(jstream));
        RetainPtr<CFX_DIBBase> dec = jimg->LoadDIBBase();
        if (dec && dec->GetWidth() == w && dec->GetHeight() == h) {
          const int dcomps = std::max(1, dec->GetBPP() / 8);
          double err_sum = 0;
          size_t cnt = 0;
          for (int y = 0; y < h; ++y) {
            pdfium::span<const uint8_t> drow = dec->GetScanline(y);
            if (drow.size() < static_cast<size_t>(w) * dcomps) {
              cnt = 0;
              break;
            }
            for (int x = 0; x < w; ++x) {
              const uint8_t* px = drow.data() + static_cast<size_t>(x) * dcomps;
              if (gray) {
                err_sum +=
                    std::abs(int(dst[static_cast<size_t>(y) * w + x]) -
                             int(px[0]));
              } else {
                const uint8_t* sp =
                    dst.data() + (static_cast<size_t>(y) * w + x) * 3;

                err_sum += (std::abs(int(sp[0]) - int(px[2])) +
                            std::abs(int(sp[1]) - int(px[1])) +
                            std::abs(int(sp[2]) - int(px[0]))) /
                           3.0;
              }
              ++cnt;
            }
          }
          if (cnt && err_sum / cnt <= kMaxMeanAbsErr) {
            return HyperOverwriteJpegInPlace(doc_, image, std::move(jpeg));
          }
        }
      } else {
        HyperJpegliFree(jl);
        jl = nullptr;
      }
    }
    if (jl)
      HyperJpegliFree(jl);

    return HyperEmitLosslessFlate(image, bm, gray);
  }

  RetainPtr<const CPDF_Object> HyperResampleSMask(
      const CPDF_Object* smask_ref,
      int base_src_w, int base_src_h,
      int base_target_w, int base_target_h) {
    if (!smask_ref || !smask_ref->IsReference() || base_src_w < 1 ||
        base_src_h < 1)
      return nullptr;
    RetainPtr<CPDF_Object> tgt = doc_->GetOrParseIndirectObject(
        smask_ref->AsReference()->GetRefObjNum());
    RetainPtr<CPDF_Stream> mstream = tgt ? ToStream(tgt) : nullptr;
    if (!mstream)
      return nullptr;
    RetainPtr<const CPDF_Dictionary> mdict = mstream->GetDict();
    if (!mdict)
      return nullptr;

    if (mdict->KeyExist("Matte"))
      return nullptr;
    const int mw = mdict->GetIntegerFor("Width");
    const int mh = mdict->GetIntegerFor("Height");
    if (mw < 2 || mh < 2)
      return nullptr;

    const int tw = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(mw) *
                                        base_target_w / base_src_w)));
    const int th = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(mh) *
                                        base_target_h / base_src_h)));
    if (tw >= mw || th >= mh)
      return nullptr;

    auto tmp_img = pdfium::MakeRetain<CPDF_Image>(
        doc_, mstream->GetObjNum());
    RetainPtr<CFX_DIBBase> msrc = tmp_img->LoadDIBBase();
    if (!msrc)
      return nullptr;

    auto realized = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!realized->Copy(msrc))
      return nullptr;
    if (realized->GetBPP() != 8)
      return nullptr;
    RetainPtr<CFX_DIBitmap> scaled =
        realized->StretchTo(tw, th, FXDIB_ResampleOptions(), nullptr);
    if (!scaled || scaled->GetBPP() != 8)
      return nullptr;

    std::vector<uint8_t> raw(static_cast<size_t>(tw) * th);
    for (int y = 0; y < th; ++y) {
      pdfium::span<const uint8_t> row = scaled->GetScanline(y);
      if (row.size() < static_cast<size_t>(tw))
        return nullptr;
      std::memcpy(raw.data() + static_cast<size_t>(y) * tw, row.data(), tw);
    }

    const size_t orig_stored = mstream->GetRawSize();
    uint8_t* jbuf = nullptr;
    size_t jlen = 0;
    const int q = opts_.image_quality > 0 ? opts_.image_quality : 75;
    const bool jok = HyperJpegliEncode(raw.data(), tw, th, tw,  1,
                                        0, q,  0,
                                        0, &jbuf, &jlen);
    DataVector<uint8_t> flate = FlateModule::Encode(
        pdfium::span<const uint8_t>(raw.data(), raw.size()));
    const bool use_jpeg = jok && jbuf && jlen > 0 &&
                          (flate.empty() || jlen < flate.size());
    DataVector<uint8_t> payload;
    if (use_jpeg) {
      payload.assign(jbuf, jbuf + jlen);
    } else if (!flate.empty()) {
      payload = std::move(flate);
    }
    if (jbuf)
      HyperJpegliFree(jbuf);
    if (payload.empty() || payload.size() >= orig_stored)
      return nullptr;
    auto ndict = doc_->New<CPDF_Dictionary>();
    ndict->SetNewFor<CPDF_Name>("Type", "XObject");
    ndict->SetNewFor<CPDF_Name>("Subtype", "Image");
    ndict->SetNewFor<CPDF_Number>("Width", tw);
    ndict->SetNewFor<CPDF_Number>("Height", th);
    ndict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
    ndict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
    ndict->SetNewFor<CPDF_Name>("Filter",
                                use_jpeg ? "DCTDecode" : "FlateDecode");

    if (RetainPtr<const CPDF_Object> dec = mdict->GetDirectObjectFor("Decode"))
      ndict->SetFor("Decode", dec->Clone());
    auto nstream =
        doc_->NewIndirect<CPDF_Stream>(std::move(payload), std::move(ndict));
    if (!nstream || nstream->GetObjNum() == 0)
      return nullptr;
    return pdfium::MakeRetain<CPDF_Reference>(doc_, nstream->GetObjNum());
  }

  bool MaybeRewriteImageObject(CPDF_ImageObject* image_obj,
                                const CFX_Matrix& holder_ctm) {
    RetainPtr<CPDF_Image> image = image_obj->GetImage();
    if (!image) return false;

    last_rewrite_needs_regen_ = true;

    {
      const int gw = image->GetPixelWidth();
      const int gh = image->GetPixelHeight();
      static constexpr int64_t kMaxRewritePixels = 256LL * 1024 * 1024;
      if (gw <= 0 || gh <= 0 ||
          static_cast<int64_t>(gw) * static_cast<int64_t>(gh) >
              kMaxRewritePixels) {
        last_rewrite_needs_regen_ = false;
        return false;
      }
    }

    RetainPtr<const CPDF_Dictionary> img_dict =
        image->GetStream() ? image->GetStream()->GetDict()
                            : RetainPtr<const CPDF_Dictionary>();

    const bool wants_gray_early =
        opts_.image_grayscale != 0 ||
        opts_.image_color_target == HYPERC_COLOR_TARGET_GRAY;

    RetainPtr<const CPDF_Object> preserved_smask_ref;
    RetainPtr<const CPDF_Object> preserved_mask_ref;
    if (img_dict) {

      static constexpr const char* const kHardSkipKeys[] = {
          "Alternates", "OPI", "OC",
      };
      for (const char* k : kHardSkipKeys) {
        if (img_dict->KeyExist(k)) {
          return false;
        }
      }

      if (img_dict->KeyExist("SMask")) {
        preserved_smask_ref = img_dict->GetObjectFor("SMask");
      }

      if (img_dict->KeyExist("Mask")) {
        RetainPtr<const CPDF_Object> mask_obj = img_dict->GetObjectFor("Mask");
        if (!mask_obj || !mask_obj->IsReference()) {
          return false;
        }
        preserved_mask_ref = std::move(mask_obj);
      }
    }

    RetainPtr<CFX_DIBBase> source = image->LoadDIBBase();
    if (!source) return false;

    if (source->GetBPP() > 1) {
      auto realized = pdfium::MakeRetain<CFX_DIBitmap>();
      if (!realized->Copy(source)) return false;
      if (source->HasPalette())
        realized->SetPalette(source->GetPaletteSpan());
      source = std::move(realized);
    }

    if (source->IsMaskFormat() ||
        (image->IsMask() && source->GetBPP() == 1)) {

      const bool stencil_ok =
          source->GetBPP() == 1 &&
          (opts_.image_encoding == HYPERC_IMAGE_ENCODING_AUTO ||
           opts_.image_encoding == HYPERC_IMAGE_ENCODING_PRESERVE) &&
          HyperEmitJbig2Stencil(doc_, image, source);
      if (stencil_ok) {
        last_rewrite_needs_regen_ = false;
        return true;
      }
      return false;
    }

    bool source_is_indexed = false;

    bool source_is_spot = false;
    int spot_ncomp = 0;
    RetainPtr<const CPDF_Dictionary> spot_img_dict;
    {
      RetainPtr<const CPDF_Stream> ist = image->GetStream();
      RetainPtr<const CPDF_Dictionary> idict = ist ? ist->GetDict() : nullptr;
      spot_img_dict = idict;
      RetainPtr<const CPDF_Object> ics =
          idict ? idict->GetDirectObjectFor("ColorSpace") : nullptr;
      const CPDF_Array* icsa = ics ? ics->AsArray() : nullptr;
      if (icsa && icsa->size() >= 1) {
        RetainPtr<const CPDF_Object> head = icsa->GetObjectAt(0);
        const CPDF_Name* hn = head ? head->AsName() : nullptr;
        const ByteString csname = hn ? hn->GetString() : ByteString();
        if (csname == "Indexed") {
          source_is_indexed = true;
          if (opts_.image_encoding != HYPERC_IMAGE_ENCODING_AUTO)
            return false;
        } else if (csname == "Separation") {
          source_is_spot = true;
          spot_ncomp = 1;
          if (opts_.image_encoding != HYPERC_IMAGE_ENCODING_AUTO)
            return false;
        } else if (csname == "DeviceN") {

          RetainPtr<const CPDF_Object> names = icsa->GetDirectObjectAt(1);
          const CPDF_Array* na = names ? names->AsArray() : nullptr;
          if (na && na->size() >= 1) {
            source_is_spot = true;
            spot_ncomp = static_cast<int>(na->size());
          }
          if (opts_.image_encoding != HYPERC_IMAGE_ENCODING_AUTO)
            return false;
        }
      }
    }

    const int src_w = source->GetWidth();
    const int src_h = source->GetHeight();
    if (src_w < 2 || src_h < 2) return false;

    if (src_w < 16 || src_h < 16) return false;

    if (source_is_indexed) {
      const int64_t px = static_cast<int64_t>(src_w) * src_h;
      RetainPtr<const CPDF_Stream> ixs = image->GetStream();
      const int64_t enc =
          ixs ? static_cast<int64_t>(ixs->GetRawSize()) : 0;
      const bool large = px >= 500000;
      const bool dense = px > 0 && enc * 5 >= px * 2;

      const CFX_Matrix on_page_idx = image_obj->matrix() * holder_ctm;
      const float wlen_idx = std::max(std::hypot(on_page_idx.a, on_page_idx.b), 1.0f);
      const float hlen_idx = std::max(std::hypot(on_page_idx.c, on_page_idx.d), 1.0f);
      const float edpi_idx = std::max(src_w * 72.0f / wlen_idx,
                                      src_h * 72.0f / hlen_idx);
      const bool hi_dpi =
          opts_.image_max_dpi > 0 &&
          edpi_idx > static_cast<float>(opts_.image_max_dpi) * 1.5f;

      const bool substantial = px >= 30000;
      if (!large && !dense && !hi_dpi && !substantial)
        return false;
    }

    if (source_is_indexed && source->GetBPP() <= 8) {
      RetainPtr<CFX_DIBitmap> rgb = source->ConvertTo(FXDIB_Format::kBgr);
      if (!rgb) return false;
      source = std::move(rgb);
    }

    const bool source_photographic =
        source_is_indexed && HyperIndexedIsPhotographic(source.Get());

    const CFX_Matrix& m = image_obj->matrix();
    const CFX_Matrix on_page = m * holder_ctm;

    const float drawn_w_pts = std::max(std::hypot(on_page.a, on_page.b), 1.0f);
    const float drawn_h_pts = std::max(std::hypot(on_page.c, on_page.d), 1.0f);
    const float dpi_x = static_cast<float>(src_w) * 72.0f / drawn_w_pts;
    const float dpi_y = static_cast<float>(src_h) * 72.0f / drawn_h_pts;
    const float effective_dpi = std::max(dpi_x, dpi_y);

    const bool image_upscaled =
        drawn_w_pts > static_cast<float>(src_w) + 1.0f ||
        drawn_h_pts > static_cast<float>(src_h) + 1.0f;

    int class_max_dpi = opts_.image_max_dpi;
    {
      const int cls_bpp = source->GetBPP();
      if (cls_bpp == 1) {
        if (opts_.image_mono_max_dpi > 0) class_max_dpi = opts_.image_mono_max_dpi;
      } else if (cls_bpp <= 8) {
        if (opts_.image_gray_max_dpi > 0) class_max_dpi = opts_.image_gray_max_dpi;
      } else if (opts_.image_color_max_dpi > 0) {
        class_max_dpi = opts_.image_color_max_dpi;
      }
    }
    const float max_dpi = static_cast<float>(class_max_dpi);

    const float threshold_dpi =
        opts_.image_threshold_dpi > 0
            ? static_cast<float>(opts_.image_threshold_dpi)
            : max_dpi * (HyperImageSourceIsLossy(image) ? 1.0f : 1.5f);
    int target_w = src_w;
    int target_h = src_h;
    bool needs_resize = false;

    if (max_dpi > 0.0f && effective_dpi > threshold_dpi &&
        source->GetBPP() > 1) {
      const float scale = max_dpi / effective_dpi;
      target_w = std::max(1, static_cast<int>(std::round(src_w * scale)));
      target_h = std::max(1, static_cast<int>(std::round(src_h * scale)));
      needs_resize = true;
    }

    if (source_is_spot && needs_resize) {
      if (HyperEmitSpotResampled(image, spot_img_dict, src_w, src_h, target_w,
                                  target_h, spot_ncomp)) {
        image_obj->CalcBoundingBox();
        image_obj->SetDirty(true);
        last_rewrite_needs_regen_ = false;
        return true;
      }
      return false;
    }

    if (source_is_spot) {
      return false;
    }

    const bool wants_gray =
        opts_.image_grayscale != 0 ||
        opts_.image_color_target == HYPERC_COLOR_TARGET_GRAY;
    const bool needs_grayscale = wants_gray && source->GetBPP() > 8;
    const bool wants_cmyk =
        opts_.image_color_target == HYPERC_COLOR_TARGET_CMYK &&
        source->GetBPP() >= 24;

    CFX_Matrix effective_matrix = m;
    bool was_clipped = false;
    RetainPtr<CFX_DIBitmap> clipped_source;
    if (opts_.clip_images) {

      auto src_copy = pdfium::MakeRetain<CFX_DIBitmap>();
      if (src_copy->Copy(source)) {

        source = src_copy;
        clipped_source = HyperClipBitmapToVisible(
            src_copy, m, image_obj->clip_path(), &effective_matrix);
        if (clipped_source && clipped_source.Get() != src_copy.Get()) {
          was_clipped = true;
        } else {
          clipped_source = nullptr;
        }
      }
    }
    if (was_clipped) {
      source = clipped_source;
    }

    const bool only_color_complexity =
        opts_.reduce_color_complexity && !needs_resize && !needs_grayscale &&
        !was_clipped && !wants_cmyk &&
        opts_.image_encoding == HYPERC_IMAGE_ENCODING_PRESERVE;

    auto image_filter_is_jbig2 = [](const RetainPtr<CPDF_Image>& img) -> bool {
      RetainPtr<const CPDF_Stream> s = img ? img->GetStream() : nullptr;
      RetainPtr<const CPDF_Dictionary> d = s ? s->GetDict() : nullptr;
      if (!d) return false;
      RetainPtr<const CPDF_Object> f = d->GetDirectObjectFor("Filter");
      if (!f) return false;
      if (f->IsName()) return f->GetString() == "JBIG2Decode";
      if (const CPDF_Array* arr = f->AsArray()) {
        for (size_t i = 0; i < arr->size(); ++i) {
          RetainPtr<const CPDF_Object> e = arr->GetDirectObjectAt(i);
          if (e && e->IsName() && e->GetString() == "JBIG2Decode") return true;
        }
      }
      return false;
    };
    const bool wants_mono_recompress =
        source->GetBPP() == 1 &&
        (opts_.image_encoding == HYPERC_IMAGE_ENCODING_AUTO ||
         opts_.image_encoding == HYPERC_IMAGE_ENCODING_PRESERVE) &&
        !image_filter_is_jbig2(image);
    if (!needs_resize && !needs_grayscale && !was_clipped && !wants_cmyk &&
        !opts_.reduce_color_complexity && !wants_mono_recompress) {
      return false;
    }

    if (source_is_indexed && !needs_resize && !needs_grayscale &&
        !was_clipped && !wants_cmyk && !source_photographic) {
      return false;
    }

    RetainPtr<CFX_DIBitmap> final_bitmap;
    if (needs_resize) {

      FXDIB_ResampleOptions resample_opts;
      if (opts_.image_resample_quality == HYPERC_RESAMPLE_NEAREST) {
        resample_opts.bNoSmoothing = true;
      } else if (opts_.image_resample_quality == HYPERC_RESAMPLE_HIGH) {
        resample_opts.bInterpolateBilinear = true;
      }
      final_bitmap = source->StretchTo(target_w, target_h,
                                        resample_opts, nullptr);
      if (!final_bitmap) return false;
    } else {

      final_bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
      if (!final_bitmap->Copy(source)) return false;
    }

    if (needs_resize && !was_clipped && preserved_smask_ref) {
      if (RetainPtr<const CPDF_Object> resampled = HyperResampleSMask(
              preserved_smask_ref.Get(), src_w, src_h, target_w, target_h)) {
        preserved_smask_ref = std::move(resampled);
      }
    }
    if (needs_grayscale) {

      RetainPtr<CFX_DIBitmap> gray = HyperConvertToICCGray(final_bitmap);
      if (gray) {
        final_bitmap = std::move(gray);
      } else {

        return false;
      }
    }

    uint32_t shared_objnum =
        image && image->GetStream() ? image->GetStream()->GetObjNum() : 0;
    auto rc_it = ref_count_.find(shared_objnum);
    const bool stream_shared = shared_objnum != 0 &&
                               rc_it != ref_count_.end() && rc_it->second > 1;
    if (image && image->GetStream() &&
        (was_clipped || (needs_grayscale && stream_shared))) {
      auto cow = pdfium::MakeRetain<CPDF_Image>(doc_);
      cow->SetImage(final_bitmap);
      if (cow->GetStream()) {
        image_obj->SetImage(cow);
        image = std::move(cow);
      }

    }

    auto reattach_masks = [&]() {
      if (!preserved_smask_ref && !preserved_mask_ref) return;
      RetainPtr<const CPDF_Stream> cs = image ? image->GetStream() : nullptr;
      if (!cs) return;

      CPDF_Dictionary* rd =
          const_cast<CPDF_Stream*>(cs.Get())->GetMutableDict().Get();
      if (!rd) return;
      if (preserved_smask_ref && !rd->KeyExist("SMask"))
        rd->SetFor("SMask", preserved_smask_ref->Clone());
      if (preserved_mask_ref && !rd->KeyExist("Mask"))
        rd->SetFor("Mask", preserved_mask_ref->Clone());
    };

    if (opts_.image_color_target == HYPERC_COLOR_TARGET_CMYK &&
        final_bitmap && final_bitmap->GetBPP() >= 24) {
      if (was_clipped) {
        image_obj->SetImageMatrix(effective_matrix);
      }
      if (HyperConvertAndEmitCmyk(doc_, image, final_bitmap)) {
        reattach_masks();
        image_obj->CalcBoundingBox();
        image_obj->SetDirty(true);
        return true;
      }

    }

    if (was_clipped) {
      image_obj->SetImageMatrix(effective_matrix);
    }

    if (needs_grayscale && (preserved_smask_ref || preserved_mask_ref)) {
      if (final_bitmap && final_bitmap->GetBPP() == 8 &&
          HyperConvertAndEmitGray(doc_, image, final_bitmap,
                                  preserved_smask_ref, preserved_mask_ref)) {
        image_obj->CalcBoundingBox();
        image_obj->SetDirty(true);
        return true;
      }
      return false;
    }

    bool emitted = false;
    const int encoding = opts_.image_encoding;

    if (encoding == HYPERC_IMAGE_ENCODING_AUTO ||
        encoding == HYPERC_IMAGE_ENCODING_PRESERVE) {

      if (opts_.reduce_color_complexity) {

        if (!image_upscaled &&
            effective_dpi >= static_cast<float>(opts_.image_index_min_dpi) &&
            !preserved_smask_ref && !preserved_mask_ref && !source_is_indexed) {
          emitted = HyperTryEmitIndexed(doc_, image, final_bitmap,
                                       (opts_.pdfa_mode & 2) != 0);

          if (emitted) last_rewrite_needs_regen_ = false;
        }

        if (!emitted && opts_.image_lossy_index > 0 &&
            encoding == HYPERC_IMAGE_ENCODING_AUTO && !image_upscaled &&
            effective_dpi >= 72.0f && !preserved_smask_ref &&
            !preserved_mask_ref && !source_is_indexed) {
          emitted = HyperTryEmitLossyIndexed(
              doc_, image, final_bitmap,
              static_cast<double>(opts_.image_lossy_index), opts_.image_quality,
              opts_.jpeg_subsample, (opts_.pdfa_mode & 2) != 0);
          if (emitted) last_rewrite_needs_regen_ = false;
        }
        if (!emitted && only_color_complexity) {

          return false;
        }
      }

      if (!emitted && source_is_indexed && needs_resize && source &&
          !source_photographic && final_bitmap->GetBPP() >= 24 &&
          HyperBitmapIsLineArt(source.Get())) {
        const bool gray = HyperBitmapIsAchromatic(source.Get());

        if (HyperEmitLineArtCompete(image, final_bitmap, gray)) {
          emitted = true;
          last_rewrite_needs_regen_ = false;
        } else {
          return false;
        }
      }

      if (!emitted && final_bitmap->GetBPP() == 1) {
        pending_jbig2_.push_back({image, final_bitmap});
        emitted = true;

        last_rewrite_needs_regen_ = false;
      }

      if (!emitted) {
        const bool transformed =
            needs_resize || needs_grayscale || was_clipped;
        const bool cow_pointed = was_clipped || needs_grayscale;
        if (!transformed && !HyperImageSourceIsLossy(image) &&
            !HyperImageSourceIsRawRecompressible(image) &&
            !source_photographic) {

          return false;
        }
        size_t budget = 0;
        if (!cow_pointed) {
          RetainPtr<const CPDF_Stream> os = image->GetStream();
          if (os)
            budget = os->GetRawSize();
        }
        emitted = HyperEmitJpeg(doc_, image, final_bitmap, opts_.image_quality,
                                 opts_.jpeg_subsample,
                                 opts_.jpeg_optimized_huffman != 0,
                                 opts_.jpeg_progressive != 0, budget);

        if (opts_.image_prefer_jpx) {
          size_t jpx_budget =
              (emitted && image->GetStream()) ? image->GetStream()->GetRawSize()
                                              : budget;
          if (HyperEmitJpeg2000(doc_, image, final_bitmap, opts_.image_quality,
                                jpx_budget))
            emitted = true;
        }

        if (emitted && !cow_pointed) {
          last_rewrite_needs_regen_ = false;
        }
        if (!emitted && !cow_pointed) {

          return false;
        }
      }
    } else if (encoding == HYPERC_IMAGE_ENCODING_JPEG) {
      emitted = HyperEmitJpeg(doc_, image, final_bitmap, opts_.image_quality,
                               opts_.jpeg_subsample,
                               opts_.jpeg_optimized_huffman != 0,
                               opts_.jpeg_progressive != 0);
    } else if (encoding == HYPERC_IMAGE_ENCODING_JPEG2000) {
      emitted = HyperEmitJpeg2000(doc_, image, final_bitmap,
                                   opts_.image_quality);
    } else if (encoding == HYPERC_IMAGE_ENCODING_FLATE) {

      image->SetImage(final_bitmap);
      emitted = true;
    }
    if (!emitted) {

      image->SetImage(final_bitmap);
    }
    reattach_masks();
    image_obj->CalcBoundingBox();
    image_obj->SetDirty(true);
    return true;
  }

  struct PendingJbig2 {
    RetainPtr<CPDF_Image> image;
    RetainPtr<CFX_DIBitmap> bitmap;
  };
  std::vector<PendingJbig2> pending_jbig2_;

  bool last_rewrite_needs_regen_ = true;

  std::vector<double> Jbig2InkGrid(const CFX_DIBBase* b) {

    constexpr int G = 32;
    if (!b) return {};
    const int w = b->GetWidth(), h = b->GetHeight();
    if (w <= 0 || h <= 0) return {};
    const int bpp = b->GetBPP();
    auto lum = [](uint32_t argb) -> int {
      int r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, bl = argb & 0xff;
      return (r * 30 + g * 59 + bl * 11) / 100;
    };
    uint32_t pal0 = 0, pal1 = 0;
    if (bpp == 1) { pal0 = b->GetPaletteArgb(0); pal1 = b->GetPaletteArgb(1); }
    std::vector<int64_t> dark(G * G, 0), total(G * G, 0);
    const int xstep = std::max(1, w / 512);
    const int ystep = std::max(1, h / 512);
    for (int y = 0; y < h; y += ystep) {
      pdfium::span<const uint8_t> row = b->GetScanline(y);
      if (row.empty()) continue;
      const int gy = std::min(G - 1, y * G / h);
      for (int x = 0; x < w; x += xstep) {
        int l;
        if (bpp == 1) {
          int bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
          l = lum(bit ? pal1 : pal0);
        } else if (bpp >= 24) {
          size_t off = static_cast<size_t>(x) * (bpp / 8);
          if (off + 2 >= row.size()) continue;
          l = lum((static_cast<uint32_t>(row[off + 2]) << 16) |
                  (static_cast<uint32_t>(row[off + 1]) << 8) | row[off]);
        } else {
          if (static_cast<size_t>(x) >= row.size()) continue;
          l = row[x];
        }
        const int cell = gy * G + std::min(G - 1, x * G / w);
        ++total[cell];
        if (l < 128) ++dark[cell];
      }
    }
    std::vector<double> grid(G * G, 0.0);
    for (int i = 0; i < G * G; ++i)
      grid[i] = total[i] ? static_cast<double>(dark[i]) / total[i] : 0.0;
    return grid;
  }

  static constexpr double kMaxJbig2SubstDiff = 0.60;

  bool DecodeJbig2RawToGrid(pdfium::span<const uint8_t> globals_span,
                            pdfium::span<const uint8_t> page_span,
                            int w, int h, const CFX_DIBBase* src_bitmap,
                            std::vector<double>* out, double* out_ink_diff) {
    if (w <= 0 || h <= 0) return false;

    const uint32_t pitch = ((static_cast<uint32_t>(w) + 31) / 32) * 4;
    std::vector<uint8_t> dest(static_cast<size_t>(pitch) * h, 0);
    auto doc_ctx = std::make_unique<JBig2_DocumentContext>();
    Jbig2Context ctx;

    FXCODEC_STATUS st = Jbig2Decoder::StartDecode(
        &ctx, doc_ctx.get(), static_cast<uint32_t>(w),
        static_cast<uint32_t>(h), page_span,  2, globals_span,
         1, pdfium::span<uint8_t>(dest), pitch, nullptr,
         false);
    while (st == FXCODEC_STATUS::kDecodeToBeContinued)
      st = Jbig2Decoder::ContinueDecode(&ctx, nullptr);
    if (st != FXCODEC_STATUS::kDecodeFinished) return false;

    constexpr int G = 32;
    std::vector<int64_t> set(G * G, 0), tot(G * G, 0);
    for (int y = 0; y < h; ++y) {
      const uint8_t* row = dest.data() + static_cast<size_t>(y) * pitch;
      const int gy = std::min(G - 1, y * G / h);
      for (int x = 0; x < w; ++x) {
        const int bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
        const int cell = gy * G + std::min(G - 1, x * G / w);
        ++tot[cell];
        if (bit) ++set[cell];
      }
    }
    out->assign(G * G, 0.0);
    for (int i = 0; i < G * G; ++i)
      (*out)[i] = tot[i] ? static_cast<double>(set[i]) / tot[i] : 0.0;

    if (src_bitmap && out_ink_diff) {
      *out_ink_diff = 1.0;
      const int src_bpp = src_bitmap->GetBPP();
      if (src_bpp == 1 && src_bitmap->GetWidth() == w &&
          src_bitmap->GetHeight() == h) {
        int64_t xor_cnt = 0, set_src = 0, set_out = 0;
        const int64_t total = static_cast<int64_t>(w) * h;
        bool rows_ok = true;
        for (int y = 0; y < h; ++y) {
          pdfium::span<const uint8_t> srow = src_bitmap->GetScanline(y);
          const uint8_t* orow = dest.data() + static_cast<size_t>(y) * pitch;
          if (srow.size() < static_cast<size_t>((w + 7) / 8)) {
            rows_ok = false;
            break;
          }
          for (int x = 0; x < w; ++x) {
            const int sb = (srow[x >> 3] >> (7 - (x & 7))) & 1;
            const int ob = (orow[x >> 3] >> (7 - (x & 7))) & 1;
            set_src += sb;
            set_out += ob;
            xor_cnt += (sb != ob);
          }
        }
        if (rows_ok && total > 0) {
          const int64_t diff = std::min(xor_cnt, total - xor_cnt);
          const int64_t ink =
              std::max<int64_t>(std::min(set_src, total - set_src), 1);
          *out_ink_diff = static_cast<double>(diff) / static_cast<double>(ink);
        }
      }
    }
    return true;
  }

  double Jbig2GridDistance(std::vector<double> a, std::vector<double> b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 1.0;
    auto to_minority = [](std::vector<double>& g) {
      double m = 0.0;
      for (double v : g) m += v;
      m /= static_cast<double>(g.size());
      if (m > 0.5)
        for (double& v : g) v = 1.0 - v;
    };
    to_minority(a);
    to_minority(b);
    auto sum_of = [](const std::vector<double>& g) {
      double s = 0.0;
      for (double v : g) s += v;
      return s;
    };
    const double sa = sum_of(a), sb = sum_of(b);
    if (sa < 1e-6 || sb < 1e-6) return 0.0;
    double tv = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
      const double d = a[i] / sa - b[i] / sb;
      tv += d < 0.0 ? -d : d;
    }
    return tv * 0.5;
  }

  void FinaliseJbig2Batch() {
    if (pending_jbig2_.empty()) return;
    std::vector<size_t> all;
    all.reserve(pending_jbig2_.size());
    for (size_t i = 0; i < pending_jbig2_.size(); ++i)
      if (pending_jbig2_[i].bitmap) all.push_back(i);
    FinaliseJbig2Group(all);
    pending_jbig2_.clear();
  }

  bool Jbig2BatchLikelyWins(const std::vector<size_t>& group) {
    const size_t sample_n = std::min<size_t>(group.size(), 32);
    if (sample_n < 6) return true;
    void* ctx = HyperJbig2BeginDoc();
    if (!ctx) return true;
    std::vector<size_t> added;
    size_t orig = 0;
    for (size_t k = 0; k < sample_n; ++k) {
      const PendingJbig2& p = pending_jbig2_[group[k]];
      if (!p.bitmap) continue;
      pdfium::span<const uint8_t> buf = p.bitmap->GetBuffer();
      if (buf.empty()) continue;
      if (HyperJbig2AddPage(ctx, buf.data(), p.bitmap->GetPitch(),
                             p.bitmap->GetWidth(), p.bitmap->GetHeight())) {
        added.push_back(group[k]);
        if (RetainPtr<const CPDF_Stream> os = p.image->GetStream())
          orig += os->GetRawSize();
      }
    }
    if (added.empty() || orig == 0) {
      HyperJbig2EndDoc(ctx);
      return true;
    }
    uint8_t* globals = nullptr;
    size_t globals_len = 0;
    if (!HyperJbig2FinishDoc(ctx, &globals, &globals_len)) {
      if (globals) HyperJbig2Free(globals);
      HyperJbig2EndDoc(ctx);
      return true;
    }
    size_t jb = globals_len;
    for (size_t i = 0; i < added.size(); ++i) {
      uint8_t* pb = nullptr;
      size_t pl = 0;
      if (HyperJbig2GetPage(ctx, static_cast<int>(i), &pb, &pl) && pb) {
        jb += pl;
        HyperJbig2Free(pb);
      }
    }
    if (globals) HyperJbig2Free(globals);
    HyperJbig2EndDoc(ctx);
    return static_cast<double>(jb) <
           static_cast<double>(orig) * 0.85;
  }

  void FinaliseJbig2Group(const std::vector<size_t>& group) {
    if (group.empty()) return;

    if (!Jbig2BatchLikelyWins(group)) return;
    void* ctx = HyperJbig2BeginDoc();
    if (!ctx) {
      for (size_t i : group) FallbackJbig2OneImage(pending_jbig2_[i]);
      return;
    }

    std::vector<size_t> in_batch;
    in_batch.reserve(group.size());
    for (size_t i : group) {
      const PendingJbig2& p = pending_jbig2_[i];
      if (!p.bitmap) continue;
      pdfium::span<const uint8_t> buf = p.bitmap->GetBuffer();
      if (buf.empty()) continue;
      if (HyperJbig2AddPage(ctx, buf.data(),
                             p.bitmap->GetPitch(),
                             p.bitmap->GetWidth(),
                             p.bitmap->GetHeight())) {
        in_batch.push_back(i);
      } else {

        FallbackJbig2OneImage(p);
      }
    }
    if (in_batch.empty()) {
      HyperJbig2EndDoc(ctx);
      return;
    }
    uint8_t* globals = nullptr;
    size_t globals_len = 0;
    if (!HyperJbig2FinishDoc(ctx, &globals, &globals_len) ||
        !globals || globals_len == 0) {
      if (globals) HyperJbig2Free(globals);
      HyperJbig2EndDoc(ctx);
      for (size_t idx : in_batch) FallbackJbig2OneImage(pending_jbig2_[idx]);
      return;
    }

    std::vector<uint8_t> globals_copy(globals, globals + globals_len);
    HyperJbig2Free(globals);

    struct Jbig2Cand {
      size_t queue_idx;
      std::vector<uint8_t> bytes;
    };
    std::vector<Jbig2Cand> candidates;
    candidates.reserve(in_batch.size());
    size_t total_jbig2 = 0;
    size_t total_orig = 0;
    for (size_t page_index = 0; page_index < in_batch.size();
         ++page_index) {
      const size_t queue_idx = in_batch[page_index];
      PendingJbig2& p = pending_jbig2_[queue_idx];
      uint8_t* page_bytes = nullptr;
      size_t page_len = 0;
      if (!HyperJbig2GetPage(ctx, static_cast<int>(page_index),
                              &page_bytes, &page_len) ||
          !page_bytes || page_len == 0) {
        if (page_bytes) HyperJbig2Free(page_bytes);
        FallbackJbig2OneImage(p);
        continue;
      }
      std::vector<double> g_out;
      double ink_diff = 1.0;
      const bool decoded = DecodeJbig2RawToGrid(
          pdfium::span<const uint8_t>(globals_copy),
          pdfium::span<const uint8_t>(page_bytes, page_len),
          p.bitmap->GetWidth(), p.bitmap->GetHeight(), p.bitmap.Get(), &g_out,
          &ink_diff);
      const std::vector<double> g_src = Jbig2InkGrid(p.bitmap.Get());
      if (getenv("HYPER_JBIG2_DIFF_DEBUG")) {
        fprintf(stderr, "[hyper] jbig2 verify page=%zu ink_diff=%.4f tv=%.4f\n",
                candidates.size(), ink_diff,
                decoded && !g_src.empty() ? Jbig2GridDistance(g_src, g_out)
                                          : -1.0);
      }

      if (!decoded || g_src.empty() ||
          Jbig2GridDistance(g_src, g_out) > 0.30 ||
          ink_diff > kMaxJbig2SubstDiff) {
        HyperJbig2Free(page_bytes);
        FallbackJbig2OneImage(p);
        continue;
      }
      if (RetainPtr<const CPDF_Stream> os = p.image->GetStream())
        total_orig += os->GetRawSize();
      total_jbig2 += page_len;
      candidates.push_back(
          {queue_idx, std::vector<uint8_t>(page_bytes, page_bytes + page_len)});
      HyperJbig2Free(page_bytes);
    }
    HyperJbig2EndDoc(ctx);

    if (candidates.empty() ||
        globals_copy.size() + total_jbig2 >= total_orig) {
      return;
    }

    DataVector<uint8_t> globals_bytes(globals_copy.begin(), globals_copy.end());
    auto globals_dict = doc_->New<CPDF_Dictionary>();
    auto globals_stream = doc_->NewIndirect<CPDF_Stream>(
        std::move(globals_bytes), std::move(globals_dict));
    if (!globals_stream) {
      for (auto& c : candidates)
        FallbackJbig2OneImage(pending_jbig2_[c.queue_idx]);
      return;
    }
    const uint32_t globals_objnum = globals_stream->GetObjNum();
    for (auto& c : candidates) {
      PendingJbig2& p = pending_jbig2_[c.queue_idx];
      DataVector<uint8_t> data_bytes(c.bytes.begin(), c.bytes.end());
      auto dict = doc_->New<CPDF_Dictionary>();
      dict->SetNewFor<CPDF_Name>("Type", "XObject");
      dict->SetNewFor<CPDF_Name>("Subtype", "Image");
      dict->SetNewFor<CPDF_Number>("Width", p.bitmap->GetWidth());
      dict->SetNewFor<CPDF_Number>("Height", p.bitmap->GetHeight());
      dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
      dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
      dict->SetNewFor<CPDF_Name>("Filter", "JBIG2Decode");
      HyperJbig2MaybeAddDecodeFlip(dict.Get(), p.bitmap);
      auto parms = dict->SetNewFor<CPDF_Dictionary>("DecodeParms");
      parms->SetNewFor<CPDF_Reference>("JBIG2Globals", doc_, globals_objnum);
      if (!p.image->OverwriteStreamInPlace(std::move(data_bytes),
                                             std::move(dict),
                                              false)) {
        FallbackJbig2OneImage(p);
      }
    }
  }

  void FallbackJbig2BatchPerImage() {
    for (auto& p : pending_jbig2_) FallbackJbig2OneImage(p);
    pending_jbig2_.clear();
  }

  void FallbackJbig2OneImage(const PendingJbig2& p) {
    if (!p.image || !p.bitmap) return;

    if (HyperEmitJbig2(doc_, p.image, p.bitmap)) return;
    HyperEmitMonochromeCCITT(doc_, p.image, p.bitmap);
  }

  std::unordered_map<uint32_t, int> ref_count_;

  std::vector<CPDF_Form*> pending_form_regens_;

  static constexpr size_t kMaxImagesToRewrite = 5000;

  void BuildImageRefCensus() {
    ref_count_.clear();

    std::function<void(CPDF_PageObjectHolder*)> count_holder =
        [&](CPDF_PageObjectHolder* holder) {
          if (!holder) return;
          const size_t n = holder->GetActivePageObjectCount();
          for (size_t i = 0; i < n; ++i) {
            CPDF_PageObject* obj = holder->GetPageObjectByIndex(i);
            if (!obj) continue;
            if (obj->IsImage()) {
              CPDF_ImageObject* io = obj->AsImage();
              RetainPtr<CPDF_Image> img = io ? io->GetImage() : nullptr;
              RetainPtr<const CPDF_Stream> st = img ? img->GetStream() : nullptr;
              if (st && st->GetObjNum() != 0)
                ref_count_[st->GetObjNum()]++;
            } else if (obj->IsForm()) {
              CPDF_FormObject* fo = obj->AsForm();
              if (fo && fo->form())
                count_holder(fo->form());
            }
          }
        };
    const int page_count = doc_->GetPageCount();
    for (int p = 0; p < page_count; ++p) {
      RetainPtr<CPDF_Dictionary> page_dict = doc_->GetMutablePageDictionary(p);
      if (!page_dict)
        continue;
      if (!HyperIsLoadablePageDict(page_dict.Get())) continue;
      auto page = pdfium::MakeRetain<CPDF_Page>(doc_, page_dict);
      page->ParseContent();
      count_holder(page.Get());
    }
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;
  const std::unordered_set<int>& skip_pages_;
};

class HyperMrcEncoder {

  static constexpr double kMrcMaxPreviewMad = 12.0;

 public:
  HyperMrcEncoder(CPDF_Document* doc, const CompressOptions& opts)
      : doc_(doc), opts_(opts) {}

  std::unordered_set<int> Run() {
    std::unordered_set<int> handled;
    if (opts_.mrc_mode == HYPERC_MRC_OFF) return handled;

    if (opts_.image_encoding == HYPERC_IMAGE_ENCODING_PRESERVE) {
      if (getenv("HYPER_MRC_DEBUG"))
        fprintf(stderr, "[mrc] skipped: PRESERVE (lossless) intent\n");
      return handled;
    }

    const int page_count = doc_->GetPageCount();
    for (int page_index = 0; page_index < page_count; ++page_index) {
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(page_index);
      if (!page_dict) continue;
      if (!HyperIsLoadablePageDict(page_dict.Get())) continue;
      auto page = pdfium::MakeRetain<CPDF_Page>(doc_, page_dict);
      page->ParseContent();
      bool is_scanned = (opts_.mrc_mode == HYPERC_MRC_FORCE) ||
                        IsScannedPage(page.Get());
      if (getenv("HYPER_MRC_DEBUG"))
        fprintf(stderr, "[mrc] page %d scanned=%d\n", page_index, is_scanned);
      if (!is_scanned) continue;

      int pw = 0, ph = 0, qw = 0, qh = 0;
      std::vector<uint8_t> pre =
          HyperRenderPreviewGray(doc_, page_index, &pw, &ph);
      const std::vector<CPDF_Form*> no_forms;
      HyperRegenSnapshot snap = HyperCaptureRegenSnapshot(page_dict, no_forms);
      const bool rewrote = RewritePageAsMrc(page.Get());
      if (getenv("HYPER_MRC_DEBUG"))
        fprintf(stderr, "[mrc] page %d rewrite=%d\n", page_index, rewrote);
      if (rewrote) {
        HyperUnshareResourceTree(doc_, page.Get());
        CPDF_PageContentGenerator generator(page.Get());
        generator.GenerateContent();
        std::vector<uint8_t> post =
            HyperRenderPreviewGray(doc_, page_index, &qw, &qh);
        const double mad = HyperPreviewMeanAbsDiff(pre, pw, ph, post, qw, qh);
        if (getenv("HYPER_MRC_DEBUG"))
          fprintf(stderr, "[mrc] page %d preview mad=%.2f (max %.1f)\n",
                  page_index, mad, kMrcMaxPreviewMad);

        if ((mad > kMrcMaxPreviewMad || pre.empty()) &&
            !getenv("HYPER_MRC_NO_VERIFY")) {
          HyperRestoreRegenSnapshot(doc_, snap);
          if (getenv("HYPER_MRC_DEBUG"))
            fprintf(stderr, "[mrc] page %d ROLLED BACK\n", page_index);
        } else {
          handled.insert(page_index);
        }
      }
    }
    return handled;
  }

 private:
  bool IsScannedPage(CPDF_Page* page) {
    int image_count = 0;
    int text_count = 0;
    float image_area_sum = 0.0f;
    const size_t obj_count = page->GetActivePageObjectCount();
    for (size_t i = 0; i < obj_count; ++i) {
      CPDF_PageObject* obj = page->GetPageObjectByIndex(i);
      if (!obj) continue;
      if (obj->IsImage()) {
        ++image_count;
        CFX_FloatRect r = obj->GetRect();
        image_area_sum += r.Width() * r.Height();
      } else if (obj->GetType() == CPDF_PageObject::Type::kText) {

        const CPDF_TextObject* t = obj->AsText();
        if (!t || t->text_state().GetTextMode() !=
                      TextRenderingMode::MODE_INVISIBLE) {
          ++text_count;
        }
      }
    }
    if (image_count < 1 || image_count > 2) return false;
    if (text_count > 0) return false;
    CFX_FloatRect media = page->GetBBox();
    float media_area = media.Width() * media.Height();
    if (media_area <= 0) return false;
    return (image_area_sum / media_area) >= 0.80f;
  }

  bool RewritePageAsMrc(CPDF_Page* page) {

    CPDF_ImageObject* image_obj = nullptr;
    const size_t obj_count = page->GetActivePageObjectCount();
    for (size_t i = 0; i < obj_count; ++i) {
      CPDF_PageObject* obj = page->GetPageObjectByIndex(i);
      if (obj && obj->IsImage()) {
        image_obj = obj->AsImage();
        if (image_obj) break;
      }
    }
    auto mrc_dbg = [](const char* why) {
      if (getenv("HYPER_MRC_DEBUG")) fprintf(stderr, "[mrc] decline: %s\n", why);
      return false;
    };
    if (!image_obj) return mrc_dbg("no image object");
    RetainPtr<CPDF_Image> src_image = image_obj->GetImage();
    if (!src_image) return mrc_dbg("no CPDF_Image");
    RetainPtr<CFX_DIBBase> src_dib = src_image->LoadDIBBase();
    if (!src_dib) return mrc_dbg("LoadDIBBase failed");
    auto src = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!src->Copy(src_dib)) return mrc_dbg("realize Copy failed");

    if (src->GetWidth() < 2 || src->GetHeight() < 2) return mrc_dbg("degenerate dims");
    if (src->GetBPP() == 8) {

      const int gw = src->GetWidth(), gh = src->GetHeight();
      auto rgb = pdfium::MakeRetain<CFX_DIBitmap>();
      if (!rgb->Create(gw, gh, FXDIB_Format::kBgr))
        return mrc_dbg("gray expand alloc failed");
      pdfium::span<const uint32_t> pal =
          src->HasPalette() ? src->GetPaletteSpan()
                            : pdfium::span<const uint32_t>();
      for (int y = 0; y < gh; ++y) {
        pdfium::span<const uint8_t> srow = src->GetScanline(y);
        pdfium::span<uint8_t> drow = rgb->GetWritableScanline(y);
        if (srow.size() < static_cast<size_t>(gw) ||
            drow.size() < static_cast<size_t>(gw) * 3)
          return mrc_dbg("gray expand row bounds");
        for (int x = 0; x < gw; ++x) {
          const uint8_t v = srow[x];
          uint8_t b = v, g = v, r = v;
          if (!pal.empty() && v < pal.size()) {
            const uint32_t argb = pal[v];
            r = (argb >> 16) & 0xFF;
            g = (argb >> 8) & 0xFF;
            b = argb & 0xFF;
          }
          drow[x * 3] = b;
          drow[x * 3 + 1] = g;
          drow[x * 3 + 2] = r;
        }
      }
      src = std::move(rgb);
    }
    if (src->GetBPP() < 24) {

      return mrc_dbg("bpp < 24");
    }

    const int src_w = src->GetWidth();
    const int src_h = src->GetHeight();
    const int sel_dpi = opts_.mrc_selector_dpi > 0
                            ? opts_.mrc_selector_dpi
                            : 300;
    const int bg_dpi = opts_.mrc_bg_dpi > 0 ? opts_.mrc_bg_dpi : 75;
    const CFX_Matrix& m = image_obj->matrix();
    const float drawn_w_pts =
        std::max(std::fabs(m.a) + std::fabs(m.c), 1.0f);
    const float drawn_h_pts =
        std::max(std::fabs(m.b) + std::fabs(m.d), 1.0f);
    const float src_dpi = std::max(
        static_cast<float>(src_w) * 72.0f / drawn_w_pts,
        static_cast<float>(src_h) * 72.0f / drawn_h_pts);
    const int sel_w = std::max(
        1, static_cast<int>(std::round(src_w * std::min(1.0f,
            static_cast<float>(sel_dpi) / src_dpi))));
    const int sel_h = std::max(
        1, static_cast<int>(std::round(src_h * std::min(1.0f,
            static_cast<float>(sel_dpi) / src_dpi))));
    const int bg_w = std::max(
        1, static_cast<int>(std::round(src_w * std::min(1.0f,
            static_cast<float>(bg_dpi) / src_dpi))));
    const int bg_h = std::max(
        1, static_cast<int>(std::round(src_h * std::min(1.0f,
            static_cast<float>(bg_dpi) / src_dpi))));

    RetainPtr<CFX_DIBitmap> sel = MakeSelectorMask(src, sel_w, sel_h);
    if (!sel) return mrc_dbg("MakeSelectorMask null");

    RetainPtr<CFX_DIBitmap> bg = BuildBackgroundLayer(src, bg_w, bg_h, sel);
    if (!bg) return mrc_dbg("BuildBackgroundLayer null");

    RetainPtr<CFX_DIBitmap> fg = BuildForegroundLayer(src, bg_w, bg_h, sel);
    if (!fg) return mrc_dbg("BuildForegroundLayer null");

    RetainPtr<CFX_DIBitmap> photo_mask = BuildPhotoRegionMask(bg);

    size_t plain_estimate = 0;
    {
      RetainPtr<const CPDF_Stream> os = src_image->GetStream();
      plain_estimate = os ? os->GetRawSize() : 0;
    }
    if (!EmitMrcLayers(page, image_obj, src_image, bg, fg, sel,
                        opts_.mrc_bg_quality,
                        photo_mask,  75,
                        plain_estimate)) {
      return mrc_dbg("EmitMrcLayers declined (failed or lost the compete)");
    }
    return true;
  }

  RetainPtr<CFX_DIBitmap> MakeSelectorMask(
      const RetainPtr<CFX_DIBitmap>& src, int sel_w, int sel_h) {

    FXDIB_ResampleOptions opts;
    RetainPtr<CFX_DIBitmap> small =
        src->StretchTo(sel_w, sel_h, opts, nullptr);
    if (!small) return nullptr;

    if (small->GetFormat() != FXDIB_Format::kBgr &&
        !small->ConvertFormat(FXDIB_Format::kBgr)) {
      return nullptr;
    }
    {
      const int gw = small->GetWidth();
      const int gh = small->GetHeight();
      auto grey8 = pdfium::MakeRetain<CFX_DIBitmap>();
      if (!grey8->Create(gw, gh, FXDIB_Format::k8bppMask)) return nullptr;
      const int spitch = small->GetPitch();
      const int dpitch = grey8->GetPitch();
      pdfium::span<const uint8_t> sbuf = small->GetBuffer();
      pdfium::span<uint8_t> dbuf = grey8->GetWritableBuffer();
      for (int y = 0; y < gh; ++y) {
        const uint8_t* sp = sbuf.data() + static_cast<size_t>(y) * spitch;
        uint8_t* dp = dbuf.data() + static_cast<size_t>(y) * dpitch;
        for (int x = 0; x < gw; ++x) {

          uint32_t b = sp[x * 3 + 0], g = sp[x * 3 + 1], r = sp[x * 3 + 2];
          dp[x] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        }
      }
      small = std::move(grey8);
    }
    const int w = small->GetWidth();
    const int h = small->GetHeight();
    const int pitch = small->GetPitch();
    pdfium::span<const uint8_t> grey = small->GetBuffer();

    std::vector<uint64_t> sum((w + 1) * (h + 1), 0);
    std::vector<uint64_t> sqsum((w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
      uint64_t row_sum = 0;
      uint64_t row_sqsum = 0;
      for (int x = 0; x < w; ++x) {
        uint8_t v = grey[y * pitch + x];
        row_sum += v;
        row_sqsum += static_cast<uint64_t>(v) * v;
        sum[(y + 1) * (w + 1) + (x + 1)] =
            sum[y * (w + 1) + (x + 1)] + row_sum;
        sqsum[(y + 1) * (w + 1) + (x + 1)] =
            sqsum[y * (w + 1) + (x + 1)] + row_sqsum;
      }
    }
    auto rect_sum = [&](int x0, int y0, int x1, int y1) -> uint64_t {
      return sum[(y1 + 1) * (w + 1) + (x1 + 1)] -
             sum[y0 * (w + 1) + (x1 + 1)] -
             sum[(y1 + 1) * (w + 1) + x0] + sum[y0 * (w + 1) + x0];
    };
    auto rect_sqsum = [&](int x0, int y0, int x1, int y1) -> uint64_t {
      return sqsum[(y1 + 1) * (w + 1) + (x1 + 1)] -
             sqsum[y0 * (w + 1) + (x1 + 1)] -
             sqsum[(y1 + 1) * (w + 1) + x0] +
             sqsum[y0 * (w + 1) + x0];
    };

    auto mask = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!mask->Create(w, h, FXDIB_Format::k1bppRgb)) return nullptr;
    const int mask_pitch = mask->GetPitch();
    pdfium::span<uint8_t> mask_span = mask->GetWritableBuffer();
    std::memset(mask_span.data(), 0, mask_span.size());

    const int radius = 8;
    const double k = 0.34;
    const double R = 128.0;
    for (int y = 0; y < h; ++y) {
      int y0 = std::max(0, y - radius);
      int y1 = std::min(h - 1, y + radius);
      uint8_t* row = mask_span.data() + y * mask_pitch;
      for (int x = 0; x < w; ++x) {
        int x0 = std::max(0, x - radius);
        int x1 = std::min(w - 1, x + radius);
        int n = (y1 - y0 + 1) * (x1 - x0 + 1);
        double mean = static_cast<double>(rect_sum(x0, y0, x1, y1)) / n;
        double mean_sq =
            static_cast<double>(rect_sqsum(x0, y0, x1, y1)) / n;
        double variance = std::max(0.0, mean_sq - mean * mean);
        double sigma = std::sqrt(variance);
        double thresh = mean * (1.0 + k * (sigma / R - 1.0));
        uint8_t v = grey[y * pitch + x];

        if (v < thresh) {
          row[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
        }
      }
    }
    return mask;
  }

  RetainPtr<CFX_DIBitmap> BuildBackgroundLayer(
      const RetainPtr<CFX_DIBitmap>& src, int bg_w, int bg_h,
      const RetainPtr<CFX_DIBitmap>& selector) {
    FXDIB_ResampleOptions opts;
    RetainPtr<CFX_DIBitmap> bg =
        src->StretchTo(bg_w, bg_h, opts, nullptr);
    if (!bg) return nullptr;

    RetainPtr<CFX_DIBitmap> sel_small =
        selector->StretchTo(bg_w, bg_h, opts, nullptr);

    if (!sel_small ||
        (sel_small->GetBPP() != 1 && sel_small->GetBPP() != 8)) {

      return bg;
    }

    const int pitch = bg->GetPitch();
    pdfium::span<uint8_t> dst = bg->GetWritableBuffer();
    pdfium::span<const uint8_t> selb = sel_small->GetBuffer();
    const int sel_pitch = sel_small->GetPitch();
    const bool sel_1bpp = sel_small->GetBPP() == 1;
    const int bpp = bg->GetBPP() / 8;
    if (bpp < 3) return bg;
    auto is_text = [&](int x, int y) -> bool {
      if (sel_1bpp) {
        uint8_t b = selb[y * sel_pitch + x / 8];
        return (b & (0x80 >> (x % 8))) != 0;
      }
      return selb[y * sel_pitch + x] >= 128;
    };
    for (int y = 0; y < bg_h; ++y) {
      for (int x = 0; x < bg_w; ++x) {
        if (!is_text(x, y)) continue;
        int r_acc = 0, g_acc = 0, b_acc = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
          int yy = y + dy;
          if (yy < 0 || yy >= bg_h) continue;
          for (int dx = -2; dx <= 2; ++dx) {
            int xx = x + dx;
            if (xx < 0 || xx >= bg_w) continue;
            if (is_text(xx, yy)) continue;
            const uint8_t* p = dst.data() + yy * pitch + xx * bpp;
            b_acc += p[0];
            g_acc += p[1];
            r_acc += p[2];
            ++n;
          }
        }
        if (n > 0) {
          uint8_t* p = dst.data() + y * pitch + x * bpp;
          p[0] = static_cast<uint8_t>(b_acc / n);
          p[1] = static_cast<uint8_t>(g_acc / n);
          p[2] = static_cast<uint8_t>(r_acc / n);
        }
      }
    }
    return bg;
  }

  RetainPtr<CFX_DIBitmap> BuildForegroundLayer(
      const RetainPtr<CFX_DIBitmap>& src, int bg_w, int bg_h,
      const RetainPtr<CFX_DIBitmap>& selector) {
    FXDIB_ResampleOptions opts;
    RetainPtr<CFX_DIBitmap> src_small =
        src->StretchTo(bg_w, bg_h, opts, nullptr);
    if (!src_small) return nullptr;
    RetainPtr<CFX_DIBitmap> sel_small =
        selector->StretchTo(bg_w, bg_h, opts, nullptr);

    if (!sel_small ||
        (sel_small->GetBPP() != 1 && sel_small->GetBPP() != 8))
      return nullptr;
    const bool sel_1bpp = sel_small->GetBPP() == 1;

    const int src_pitch = src_small->GetPitch();
    const int sel_pitch = sel_small->GetPitch();
    pdfium::span<const uint8_t> sp = src_small->GetBuffer();
    pdfium::span<const uint8_t> selb = sel_small->GetBuffer();
    const int bpp = src_small->GetBPP() / 8;
    if (bpp < 3) return nullptr;
    auto sel_set = [&](int x, int y) -> bool {
      if (sel_1bpp) {
        uint8_t mb = selb[y * sel_pitch + x / 8];
        return (mb & (0x80 >> (x % 8))) != 0;
      }
      return selb[y * sel_pitch + x] >= 128;
    };

    std::vector<std::array<int, 3>> ink_pixels;
    ink_pixels.reserve(static_cast<size_t>(bg_w) * bg_h / 4);
    for (int y = 0; y < bg_h; ++y) {
      for (int x = 0; x < bg_w; ++x) {
        if (!sel_set(x, y)) continue;
        const uint8_t* p = sp.data() + y * src_pitch + x * bpp;
        ink_pixels.push_back({p[0], p[1], p[2]});
      }
    }

    auto fg = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!fg->Create(bg_w, bg_h, FXDIB_Format::kBgr)) return nullptr;
    pdfium::span<uint8_t> dst = fg->GetWritableBuffer();
    const int pitch = fg->GetPitch();

    if (ink_pixels.empty()) {

      std::fill(dst.begin(), dst.end(), 0xFF);
      return fg;
    }

    constexpr int kClusters = 4;
    int k = std::min(kClusters, static_cast<int>(ink_pixels.size()));
    std::vector<std::array<double, 3>> centroids(k);

    for (int c = 0; c < k; ++c) {
      const size_t idx = (ink_pixels.size() * c) / k;
      centroids[c] = {
          static_cast<double>(ink_pixels[idx][0]),
          static_cast<double>(ink_pixels[idx][1]),
          static_cast<double>(ink_pixels[idx][2]),
      };
    }
    std::vector<int> assignments(ink_pixels.size(), 0);

    for (int iter = 0; iter < 8; ++iter) {

      for (size_t i = 0; i < ink_pixels.size(); ++i) {
        double best = std::numeric_limits<double>::max();
        int best_c = 0;
        for (int c = 0; c < k; ++c) {
          const double db = ink_pixels[i][0] - centroids[c][0];
          const double dg = ink_pixels[i][1] - centroids[c][1];
          const double dr = ink_pixels[i][2] - centroids[c][2];
          const double d = db * db + dg * dg + dr * dr;
          if (d < best) { best = d; best_c = c; }
        }
        assignments[i] = best_c;
      }

      std::vector<std::array<double, 3>> sums(k, {0.0, 0.0, 0.0});
      std::vector<int> counts(k, 0);
      for (size_t i = 0; i < ink_pixels.size(); ++i) {
        const int c = assignments[i];
        sums[c][0] += ink_pixels[i][0];
        sums[c][1] += ink_pixels[i][1];
        sums[c][2] += ink_pixels[i][2];
        ++counts[c];
      }
      for (int c = 0; c < k; ++c) {
        if (counts[c] > 0) {
          centroids[c][0] = sums[c][0] / counts[c];
          centroids[c][1] = sums[c][1] / counts[c];
          centroids[c][2] = sums[c][2] / counts[c];
        }
      }
    }

    size_t ink_idx = 0;
    for (int y = 0; y < bg_h; ++y) {
      uint8_t* row = dst.data() + y * pitch;
      for (int x = 0; x < bg_w; ++x) {
        uint8_t* p = row + x * 3;
        if (sel_set(x, y)) {
          const int c = assignments[ink_idx++];
          p[0] = static_cast<uint8_t>(centroids[c][0]);
          p[1] = static_cast<uint8_t>(centroids[c][1]);
          p[2] = static_cast<uint8_t>(centroids[c][2]);
        } else {
          p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF;
        }
      }
    }
    return fg;
  }

  RetainPtr<CFX_DIBitmap> BuildPhotoRegionMask(
      const RetainPtr<CFX_DIBitmap>& bg) {
    if (!bg || bg->GetBPP() < 24) return nullptr;
    const int w = bg->GetWidth();
    const int h = bg->GetHeight();
    if (w < 16 || h < 16) return nullptr;
    const int bpp = bg->GetBPP() / 8;
    const int src_pitch = bg->GetPitch();
    pdfium::span<const uint8_t> sp = bg->GetBuffer();
    constexpr int kBlock = 8;
    const int blocks_w = (w + kBlock - 1) / kBlock;
    const int blocks_h = (h + kBlock - 1) / kBlock;
    std::vector<uint8_t> is_photo(
        static_cast<size_t>(blocks_w) * blocks_h, 0);

    bool any_photo = false;
    for (int byi = 0; byi < blocks_h; ++byi) {
      const int by = byi * kBlock;
      const int by_end = std::min(by + kBlock, h);
      for (int bxi = 0; bxi < blocks_w; ++bxi) {
        const int bx = bxi * kBlock;
        const int bx_end = std::min(bx + kBlock, w);
        double sum[3] = {0, 0, 0};
        double sum2[3] = {0, 0, 0};
        int n = 0, n_dark = 0, n_light = 0;
        for (int y = by; y < by_end; ++y) {
          const uint8_t* row = sp.data() + y * src_pitch;
          for (int x = bx; x < bx_end; ++x) {
            const uint8_t* p = row + x * bpp;
            const double lum = 0.114 * p[0] + 0.587 * p[1] +
                                0.299 * p[2];
            if (lum < 80.0) ++n_dark;
            else if (lum > 200.0) ++n_light;
            for (int ch = 0; ch < 3; ++ch) {
              sum[ch] += p[ch];
              sum2[ch] += static_cast<double>(p[ch]) * p[ch];
            }
            ++n;
          }
        }
        if (n == 0) continue;
        double total_var = 0.0;
        double sum_mean = 0.0;
        for (int ch = 0; ch < 3; ++ch) {
          double mean = sum[ch] / n;
          total_var += sum2[ch] / n - mean * mean;
          sum_mean += mean;
        }
        const double lum_mean =
            0.114 * (sum[0] / n) + 0.587 * (sum[1] / n) +
            0.299 * (sum[2] / n);
        const double bimodality =
            (static_cast<double>(n_dark) + n_light) / n;
        if (total_var > 1500.0 && bimodality < 0.80 && lum_mean < 240.0) {
          is_photo[static_cast<size_t>(byi) * blocks_w + bxi] = 1;
          any_photo = true;
        }
      }
    }
    if (!any_photo) return nullptr;

    auto mask = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!mask->Create(w, h, FXDIB_Format::k1bppRgb)) return nullptr;
    pdfium::span<uint8_t> mb = mask->GetWritableBuffer();
    const int mask_pitch = mask->GetPitch();

    std::fill(mb.begin(), mb.end(), 0xFF);
    for (int y = 0; y < h; ++y) {
      const int byi = y / kBlock;
      uint8_t* row = mb.data() + y * mask_pitch;
      for (int x = 0; x < w; ++x) {
        const int bxi = x / kBlock;
        if (is_photo[static_cast<size_t>(byi) * blocks_w + bxi]) {
          row[x / 8] &= ~(0x80 >> (x % 8));
        }
      }
    }
    return mask;
  }

  bool ScannedPageHasPhotos(const RetainPtr<CFX_DIBitmap>& src) {
    if (!src || src->GetBPP() < 24) return false;
    const int w = src->GetWidth();
    const int h = src->GetHeight();
    if (w < 64 || h < 64) return false;
    const int src_pitch = src->GetPitch();
    pdfium::span<const uint8_t> sp = src->GetBuffer();
    const int bpp = src->GetBPP() / 8;
    constexpr int kBlock = 32;

    for (int by = 0; by + kBlock <= h; by += kBlock) {
      for (int bx = 0; bx + kBlock <= w; bx += kBlock) {
        double sum[3] = {0, 0, 0};
        double sum2[3] = {0, 0, 0};
        int n = 0;
        int n_dark = 0;
        int n_light = 0;
        for (int y = by; y < by + kBlock; ++y) {
          const uint8_t* row = sp.data() + y * src_pitch;
          for (int x = bx; x < bx + kBlock; ++x) {
            const uint8_t* p = row + x * bpp;
            const double lum = 0.114 * p[0] + 0.587 * p[1] +
                                0.299 * p[2];
            if (lum < 80.0) ++n_dark;
            else if (lum > 200.0) ++n_light;
            for (int ch = 0; ch < 3; ++ch) {
              sum[ch] += p[ch];
              sum2[ch] += static_cast<double>(p[ch]) * p[ch];
            }
            ++n;
          }
        }
        if (n == 0) continue;
        double mean[3];
        double var[3];
        for (int ch = 0; ch < 3; ++ch) {
          mean[ch] = sum[ch] / n;
          var[ch] = sum2[ch] / n - mean[ch] * mean[ch];
        }
        const double total_var = var[0] + var[1] + var[2];
        const double mid_lum = 0.114 * mean[0] + 0.587 * mean[1] +
                                0.299 * mean[2];
        const double bimodality =
            (static_cast<double>(n_dark) + n_light) / n;

        if (total_var > 1500.0 && bimodality < 0.80 && mid_lum < 240.0) {
          return true;
        }
      }
    }
    return false;
  }

  static RetainPtr<CFX_DIBitmap> Invert1bpp(
      const RetainPtr<CFX_DIBitmap>& src) {
    if (!src || src->GetBPP() != 1)
      return nullptr;
    auto out = pdfium::MakeRetain<CFX_DIBitmap>();
    if (!out->Create(src->GetWidth(), src->GetHeight(), src->GetFormat()))
      return nullptr;
    const int pitch = std::min(src->GetPitch(), out->GetPitch());
    for (int y = 0; y < src->GetHeight(); ++y) {
      pdfium::span<const uint8_t> in = src->GetScanline(y);
      pdfium::span<uint8_t> dst = out->GetWritableScanline(y);
      for (int i = 0; i < pitch; ++i)
        dst[i] = static_cast<uint8_t>(~in[i]);
    }
    return out;
  }

  bool EmitMrcLayers(CPDF_Page* page, CPDF_ImageObject* orig_image_obj,
                     RetainPtr<CPDF_Image>  ,
                     const RetainPtr<CFX_DIBitmap>& bg,
                     const RetainPtr<CFX_DIBitmap>& fg,
                     const RetainPtr<CFX_DIBitmap>& selector,
                     int bg_quality,

                     const RetainPtr<CFX_DIBitmap>& photo_mask = nullptr,
                     int photo_bg_quality = 75,
                     size_t plain_budget = 0) {
    size_t bundle_total = 0;
    const CFX_Matrix& m = orig_image_obj->matrix();

    RetainPtr<CFX_DIBitmap> sel_stencil = Invert1bpp(selector);
    if (!sel_stencil) return false;
    DataVector<uint8_t> g4 = FaxModule::FaxEncode(sel_stencil);
    if (g4.empty()) return false;
    bundle_total += g4.size();

    uint8_t* bg_jpg = nullptr;
    size_t bg_len = 0;
    bool ok = false;
    UNSAFE_BUFFERS({
      ok = JpegModule::JpegEncode(bg, &bg_jpg, &bg_len, bg_quality,
                                  opts_.jpeg_subsample,
                                  opts_.jpeg_optimized_huffman != 0,
                                  opts_.jpeg_progressive != 0);
    });
    if (!ok || bg_jpg == nullptr) {
      if (bg_jpg) FX_Free(bg_jpg);
      return false;
    }
    bundle_total += bg_len;
    DataVector<uint8_t> bg_bytes(bg_jpg, UNSAFE_TODO(bg_jpg + bg_len));
    FX_Free(bg_jpg);

    uint8_t* fg_jpg = nullptr;
    size_t fg_len = 0;
    ok = false;
    UNSAFE_BUFFERS({
      ok = JpegModule::JpegEncode(fg, &fg_jpg, &fg_len,
                                  opts_.mrc_fg_quality,
                                  opts_.jpeg_subsample,
                                  opts_.jpeg_optimized_huffman != 0,
                                  opts_.jpeg_progressive != 0);
    });
    if (!ok || fg_jpg == nullptr) {
      if (fg_jpg) FX_Free(fg_jpg);
      return false;
    }
    bundle_total += fg_len;
    DataVector<uint8_t> fg_bytes(fg_jpg, UNSAFE_TODO(fg_jpg + fg_len));
    FX_Free(fg_jpg);

    DataVector<uint8_t> mask_g4;
    DataVector<uint8_t> bgp_bytes;
    if (photo_mask) {
      mask_g4 = FaxModule::FaxEncode(photo_mask);
      if (!mask_g4.empty()) {
        uint8_t* bgp_jpg = nullptr;
        size_t bgp_len = 0;
        bool bgp_ok = false;
        UNSAFE_BUFFERS({
          bgp_ok = JpegModule::JpegEncode(bg, &bgp_jpg, &bgp_len,
                                          photo_bg_quality,
                                          opts_.jpeg_subsample,
                                          opts_.jpeg_optimized_huffman != 0,
                                          opts_.jpeg_progressive != 0);
        });
        if (bgp_ok && bgp_jpg && bgp_len > 0) {
          bundle_total += mask_g4.size() + bgp_len;
          bgp_bytes = DataVector<uint8_t>(bgp_jpg,
                                          UNSAFE_TODO(bgp_jpg + bgp_len));
        } else {
          mask_g4.clear();
        }
        if (bgp_jpg) FX_Free(bgp_jpg);
      }
    }

    constexpr size_t kMrcPageOverhead = 1536;
    if (plain_budget > 0 &&
        bundle_total + kMrcPageOverhead >=
            static_cast<size_t>(plain_budget * 0.98)) {
      if (getenv("HYPER_MRC_DEBUG"))
        fprintf(stderr, "[mrc] compete lost: bundle=%zu plain=%zu\n",
                bundle_total, plain_budget);
      return false;
    }
    if (getenv("HYPER_MRC_DEBUG"))
      fprintf(stderr, "[mrc] compete won: bundle=%zu plain=%zu\n",
              bundle_total, plain_budget);

    auto sel_dict = doc_->New<CPDF_Dictionary>();
    sel_dict->SetNewFor<CPDF_Name>("Type", "XObject");
    sel_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    sel_dict->SetNewFor<CPDF_Number>("Width", selector->GetWidth());
    sel_dict->SetNewFor<CPDF_Number>("Height", selector->GetHeight());
    sel_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
    sel_dict->SetNewFor<CPDF_Boolean>("ImageMask", true);
    sel_dict->SetNewFor<CPDF_Name>("Filter", "CCITTFaxDecode");
    auto sel_parms = sel_dict->SetNewFor<CPDF_Dictionary>("DecodeParms");
    sel_parms->SetNewFor<CPDF_Number>("K", -1);
    sel_parms->SetNewFor<CPDF_Number>("Columns", selector->GetWidth());
    sel_parms->SetNewFor<CPDF_Number>("Rows", selector->GetHeight());
    auto sel_stream = doc_->NewIndirect<CPDF_Stream>(std::move(g4),
                                                      std::move(sel_dict));
    if (!sel_stream) return false;
    uint32_t sel_objnum = sel_stream->GetObjNum();

    auto bg_image = pdfium::MakeRetain<CPDF_Image>(doc_);
    {
      auto file = pdfium::MakeRetain<
          CFX_ReadOnlyContainerStream<DataVector<uint8_t>>>(
          std::move(bg_bytes));
      bg_image->SetJpegImageInline(std::move(file));
    }

    auto fg_image = pdfium::MakeRetain<CPDF_Image>(doc_);
    {
      auto file = pdfium::MakeRetain<
          CFX_ReadOnlyContainerStream<DataVector<uint8_t>>>(
          std::move(fg_bytes));
      fg_image->SetJpegImageInline(std::move(file));
    }

    {
      RetainPtr<const CPDF_Stream> fg_stream = fg_image->GetStream();
      if (fg_stream) {
        pdfium::span<const uint8_t> fg_raw_span =
            fg_stream->IsMemoryBased() ? fg_stream->GetInMemoryRawData()
                                       : pdfium::span<const uint8_t>();
        DataVector<uint8_t> fg_raw(fg_raw_span.begin(), fg_raw_span.end());
        RetainPtr<CPDF_Object> cloned_obj = fg_stream->GetDict()->Clone();
        RetainPtr<CPDF_Dictionary> cloned_dict =
            ToDictionary(cloned_obj.Get())
                ? pdfium::WrapRetain(cloned_obj->AsMutableDictionary())
                : RetainPtr<CPDF_Dictionary>();
        if (cloned_dict) {
          cloned_dict->SetNewFor<CPDF_Reference>("Mask", doc_, sel_objnum);
          auto fg_indirect = doc_->NewIndirect<CPDF_Stream>(
              std::move(fg_raw), std::move(cloned_dict));
          if (fg_indirect) {
            fg_image = pdfium::MakeRetain<CPDF_Image>(
                doc_, fg_indirect->GetObjNum());
          }
        }
      }
    }

    RetainPtr<CPDF_Image> bg_photo_image;
    if (photo_mask && !mask_g4.empty() && !bgp_bytes.empty()) {
      auto mask_dict = doc_->New<CPDF_Dictionary>();
      mask_dict->SetNewFor<CPDF_Name>("Type", "XObject");
      mask_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
      mask_dict->SetNewFor<CPDF_Number>("Width", photo_mask->GetWidth());
      mask_dict->SetNewFor<CPDF_Number>("Height", photo_mask->GetHeight());
      mask_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
      mask_dict->SetNewFor<CPDF_Boolean>("ImageMask", true);
      mask_dict->SetNewFor<CPDF_Name>("Filter", "CCITTFaxDecode");
      auto mask_parms =
          mask_dict->SetNewFor<CPDF_Dictionary>("DecodeParms");
      mask_parms->SetNewFor<CPDF_Number>("K", -1);
      mask_parms->SetNewFor<CPDF_Number>("Columns", photo_mask->GetWidth());
      mask_parms->SetNewFor<CPDF_Number>("Rows", photo_mask->GetHeight());
      auto mask_stream = doc_->NewIndirect<CPDF_Stream>(
          std::move(mask_g4), std::move(mask_dict));
      if (mask_stream) {
        const uint32_t mask_objnum = mask_stream->GetObjNum();
        auto bgp_dict = doc_->New<CPDF_Dictionary>();
        bgp_dict->SetNewFor<CPDF_Name>("Type", "XObject");
        bgp_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
        bgp_dict->SetNewFor<CPDF_Number>("Width", bg->GetWidth());
        bgp_dict->SetNewFor<CPDF_Number>("Height", bg->GetHeight());
        bgp_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
        bgp_dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceRGB");
        bgp_dict->SetNewFor<CPDF_Name>("Filter", "DCTDecode");
        bgp_dict->SetNewFor<CPDF_Reference>("Mask", doc_, mask_objnum);
        auto bgp_stream = doc_->NewIndirect<CPDF_Stream>(
            std::move(bgp_bytes), std::move(bgp_dict));
        if (bgp_stream) {
          bg_photo_image = pdfium::MakeRetain<CPDF_Image>(
              doc_, bgp_stream->GetObjNum());
        }
      }
    }

    auto bg_obj = std::make_unique<CPDF_ImageObject>();
    bg_obj->SetImage(bg_image);
    bg_obj->SetImageMatrix(m);
    bg_obj->CalcBoundingBox();
    bg_obj->SetDirty(true);

    auto fg_obj = std::make_unique<CPDF_ImageObject>();
    fg_obj->SetImage(fg_image);
    fg_obj->SetImageMatrix(m);
    fg_obj->CalcBoundingBox();
    fg_obj->SetDirty(true);

    std::unique_ptr<CPDF_PageObject> removed =
        page->RemovePageObject(orig_image_obj);
    (void)removed;
    page->AppendPageObject(std::move(bg_obj));
    if (bg_photo_image) {

      auto bgp_obj = std::make_unique<CPDF_ImageObject>();
      bgp_obj->SetImage(bg_photo_image);
      bgp_obj->SetImageMatrix(m);
      bgp_obj->CalcBoundingBox();
      bgp_obj->SetDirty(true);
      page->AppendPageObject(std::move(bgp_obj));
    }
    page->AppendPageObject(std::move(fg_obj));
    return true;
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;
};

class HyperAnnotationFlatten {
 public:
  HyperAnnotationFlatten(CPDF_Document* doc,
                          FPDF_DOCUMENT fpdf_doc,
                          const CompressOptions& opts)
      : doc_(doc), fpdf_doc_(fpdf_doc), opts_(opts) {}

  void Run() {
    const uint32_t mask = opts_.discard_mask;
    if (!(mask & (HYPERC_FLATTEN_FORMS | HYPERC_FLATTEN_LINKS |
                  HYPERC_FLATTEN_ANNOTS_KEEP_FORMS_LINKS))) {
      return;
    }
    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      FlattenPageAnnotations(i, mask);
    }

    if (mask & HYPERC_FLATTEN_FORMS) {
      RetainPtr<CPDF_Dictionary> root = doc_->GetMutableRoot();
      if (root) {
        root->RemoveFor("AcroForm");
      }
    }
  }

 private:
  void FlattenPageAnnotations(int page_index, uint32_t mask) {
    RetainPtr<CPDF_Dictionary> page_dict =
        doc_->GetMutablePageDictionary(page_index);
    if (!page_dict) return;
    RetainPtr<CPDF_Array> annots = page_dict->GetMutableArrayFor("Annots");
    if (!annots || annots->size() == 0) return;

    std::vector<RetainPtr<CPDF_Object>> to_flatten;
    std::vector<RetainPtr<CPDF_Object>> to_keep;
    to_flatten.reserve(annots->size());
    to_keep.reserve(annots->size());

    for (size_t j = 0; j < annots->size(); ++j) {
      RetainPtr<CPDF_Object> ent = annots->GetMutableObjectAt(j);
      if (!ent) continue;
      RetainPtr<CPDF_Dictionary> ad =
          ToDictionary(ent->GetMutableDirect());
      if (!ad) { to_keep.push_back(ent); continue; }
      ByteString subtype = ad->GetNameFor("Subtype");
      const bool is_widget = (subtype == "Widget");
      const bool is_link = (subtype == "Link");

      bool should_flatten = false;
      if (is_widget && (mask & HYPERC_FLATTEN_FORMS)) {

        should_flatten = true;
      } else if (is_link && (mask & HYPERC_FLATTEN_LINKS)) {

        should_flatten = true;
      } else if (!is_widget && !is_link &&
                 (mask & HYPERC_FLATTEN_ANNOTS_KEEP_FORMS_LINKS)) {

        should_flatten = true;
      }
      if (should_flatten) {
        to_flatten.push_back(ent);
      } else {
        to_keep.push_back(ent);
      }
    }

    if (to_flatten.empty()) return;

    while (annots->size() > 0) annots->RemoveAt(0);
    for (auto& a : to_flatten) annots->Append(std::move(a));

    if (fpdf_doc_) {
      FPDF_PAGE page = FPDF_LoadPage(fpdf_doc_, page_index);
      if (page) {
        FPDFPage_Flatten(page, FLAT_NORMALDISPLAY);
        FPDF_ClosePage(page);
      }
    }

    annots = page_dict->GetMutableArrayFor("Annots");
    if (annots) {
      while (annots->size() > 0) annots->RemoveAt(0);
    } else {
      annots = page_dict->SetNewFor<CPDF_Array>("Annots");
    }
    for (auto& k : to_keep) annots->Append(std::move(k));
    if (annots->size() == 0) {
      page_dict->RemoveFor("Annots");
    }
  }

  CPDF_Document* doc_;
  FPDF_DOCUMENT fpdf_doc_;
  const CompressOptions& opts_;
};

class HyperCatalogDiscard {
 public:
  HyperCatalogDiscard(CPDF_Document* doc, const CompressOptions& opts)
      : doc_(doc), opts_(opts) {}

  void Run() {
    RetainPtr<CPDF_Dictionary> root = doc_->GetMutableRoot();
    if (!root) return;
    const uint32_t mask = opts_.discard_mask;

    if (mask & HYPERC_DISCARD_STRUCT_TREE) {
      root->RemoveFor("StructTreeRoot");
      root->RemoveFor("MarkInfo");
    }
    if (mask & HYPERC_DISCARD_BOOKMARKS) {
      root->RemoveFor("Outlines");
      root->RemoveFor("PageMode");
    }
    if (mask & HYPERC_DISCARD_FORM_FIELDS) {
      root->RemoveFor("AcroForm");
    }
    if (mask & HYPERC_DISCARD_SEARCH_INDEX) {
      RetainPtr<CPDF_Dictionary> names = root->GetMutableDictFor("Names");
      if (names) {
        names->RemoveFor("IDS");
        names->RemoveFor("URLS");
      }
    }
    if (mask & HYPERC_DISCARD_FILE_ATTACHMENTS) {
      RetainPtr<CPDF_Dictionary> names = root->GetMutableDictFor("Names");
      if (names) {
        names->RemoveFor("EmbeddedFiles");
      }
      root->RemoveFor("AF");
    }
    if (mask & HYPERC_DISCARD_NAMED_DESTS) {
      root->RemoveFor("Dests");
    }
    if (mask & HYPERC_DISCARD_ACTIONS) {
      root->RemoveFor("OpenAction");
      root->RemoveFor("AA");
    }
    if (mask & HYPERC_DISCARD_PRINT_SETTING) {
      root->RemoveFor("ViewerPreferences");
    }
    if (mask & HYPERC_DISCARD_INFO_METADATA) {
      root->RemoveFor("Metadata");
      RetainPtr<CPDF_Dictionary> info = doc_->GetInfo();
      if (info) {

        static const char* const kInfoKeys[] = {
            "Title",   "Author",       "Subject", "Keywords",
            "Creator", "Producer",     "CreationDate",
            "ModDate", "Trapped",
        };
        for (const char* key : kInfoKeys) {
          info->RemoveFor(key);
        }
      }
    }

    if (mask & HYPERC_DISCARD_OUTPUT_INTENTS) {
      root->RemoveFor("OutputIntents");
    }

    if (mask & HYPERC_DISCARD_ARTICLE_THREADS) {
      root->RemoveFor("Threads");
    }

    if (mask & HYPERC_DISCARD_PIECE_INFO) {
      root->RemoveFor("PieceInfo");
    }

    if (mask & HYPERC_DISCARD_SPIDER_INFO) {
      root->RemoveFor("SpiderInfo");
    }

    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RetainPtr<CPDF_Dictionary> page = doc_->GetMutablePageDictionary(i);
      if (!page) continue;
      if (mask & HYPERC_DISCARD_THUMBNAILS) {
        page->RemoveFor("Thumb");
      }

      if (mask & HYPERC_DISCARD_ARTICLE_THREADS) {
        page->RemoveFor("B");
      }
      if (mask & HYPERC_DISCARD_ANNOTATIONS) {
        page->RemoveFor("Annots");
      }
      if (mask & HYPERC_DISCARD_PAGE_PIECE_INFO) {
        page->RemoveFor("PieceInfo");

        page->RemoveFor("Metadata");
      }
      if (mask & HYPERC_DISCARD_ACTIONS) {
        page->RemoveFor("AA");
      }
      if (mask & HYPERC_DISCARD_STRUCT_TREE) {
        page->RemoveFor("StructParents");
      }

      if ((mask & HYPERC_DISCARD_LINKS) ||
          (mask & HYPERC_DISCARD_SIG_APPEARANCES)) {
        FilterAnnotsInPlace(page.Get(), mask);
      }
    }

    if (mask & HYPERC_DISCARD_ALTERNATE_IMAGES) {
      RemoveAlternatesFromAllImages();
    }

    if (mask & HYPERC_DISCARD_PAGE_PIECE_INFO) {
      const CPDF_Dictionary* root_dict = doc_->GetRoot();
      const uint32_t last = doc_->GetLastObjNum();
      for (uint32_t objnum = 1; objnum <= last; ++objnum) {
        RetainPtr<CPDF_Object> obj = doc_->GetOrParseIndirectObject(objnum);
        if (!obj)
          continue;
        RetainPtr<CPDF_Dictionary> dict;
        if (CPDF_Stream* st = obj->AsMutableStream())
          dict = st->GetMutableDict();
        else
          dict = ToDictionary(obj);
        if (!dict || dict.Get() == root_dict)
          continue;
        if (dict->KeyExist("Metadata"))
          dict->RemoveFor("Metadata");
      }
    }
  }

 private:

  void FilterAnnotsInPlace(CPDF_Dictionary* page, uint32_t mask) {
    RetainPtr<CPDF_Array> annots = page->GetMutableArrayFor("Annots");
    if (!annots) return;
    std::vector<RetainPtr<CPDF_Object>> kept;
    kept.reserve(annots->size());
    for (size_t i = 0; i < annots->size(); ++i) {
      RetainPtr<CPDF_Object> ent = annots->GetMutableObjectAt(i);
      if (!ent) continue;
      RetainPtr<CPDF_Dictionary> ad =
          ToDictionary(ent ? ent->GetMutableDirect() : nullptr);
      if (!ad) {
        kept.push_back(ent);
        continue;
      }
      ByteString subtype = ad->GetNameFor("Subtype");
      const bool is_widget = (subtype == "Widget");
      const bool is_link = (subtype == "Link");
      if (is_widget && (mask & HYPERC_DISCARD_SIG_APPEARANCES)) {
        ByteString ft = ad->GetNameFor("FT");
        if (ft == "Sig") {
          ad->RemoveFor("AP");
        }
      }
      if (is_link && (mask & HYPERC_DISCARD_LINKS)) {
        continue;
      }
      kept.push_back(ent);
    }
    while (annots->size() > 0) {
      annots->RemoveAt(0);
    }
    for (auto& k : kept) {
      annots->Append(std::move(k));
    }
    if (annots->size() == 0) {
      page->RemoveFor("Annots");
    }
  }

  void RemoveAlternatesFromAllImages() {

    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RetainPtr<CPDF_Dictionary> page = doc_->GetMutablePageDictionary(i);
      if (!page) continue;
      RetainPtr<CPDF_Dictionary> resources =
          page->GetMutableDictFor("Resources");
      if (!resources) continue;
      RetainPtr<CPDF_Dictionary> xo =
          resources->GetMutableDictFor("XObject");
      if (!xo) continue;
      CPDF_DictionaryLocker locker(xo.Get());
      for (const auto& it : locker) {
        RetainPtr<const CPDF_Object> entry = it.second;
        if (!entry) continue;
        const CPDF_Stream* stream = nullptr;
        if (entry->IsReference()) {
          uint32_t n = entry->AsReference()->GetRefObjNum();
          RetainPtr<const CPDF_Object> target =
              doc_->GetIndirectObject(n);
          if (target && target->IsStream()) stream = target->AsStream();
        } else if (entry->IsStream()) {
          stream = entry->AsStream();
        }
        if (!stream) continue;
        const CPDF_Dictionary* d = stream->GetDict();
        if (!d || d->GetNameFor("Subtype") != "Image") continue;

        if (entry->IsReference()) {
          uint32_t n = entry->AsReference()->GetRefObjNum();
          RetainPtr<CPDF_Object> mut = doc_->GetOrParseIndirectObject(n);
          if (mut && mut->IsStream()) {
            mut->AsMutableStream()->GetMutableDict()->RemoveFor("Alternates");
          }
        }
      }
    }
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;
};

class HyperObjectDedup {
 public:
  HyperObjectDedup(CPDF_Document* doc, const CompressOptions& opts)
      : doc_(doc), opts_(opts) {}

  int Run() {
    int total = 0;
    if (opts_.optimize_resources) {
      total += DedupResources();
    }
    if (opts_.dedup_objects) {
      total += DedupAllIndirectObjects();
    }
    return total;
  }

 private:

  int DedupResources() {
    static const char* const kClasses[] = {
        "Font", "XObject", "ColorSpace", "ExtGState", "Pattern", "Shading",
    };
    std::unordered_map<uint64_t, uint32_t> seen;
    int collapsed = 0;
    const int page_count = doc_->GetPageCount();
    for (int p = 0; p < page_count; ++p) {
      RetainPtr<CPDF_Dictionary> page_dict =
          doc_->GetMutablePageDictionary(p);
      if (!page_dict) continue;
      RetainPtr<CPDF_Dictionary> resources =
          page_dict->GetMutableDictFor("Resources");
      if (!resources) continue;
      for (const char* cls : kClasses) {
        RetainPtr<CPDF_Dictionary> map = resources->GetMutableDictFor(cls);
        if (!map) continue;
        std::vector<ByteString> keys;
        {
          CPDF_DictionaryLocker locker(map.Get());
          for (const auto& it : locker) keys.push_back(it.first);
        }
        for (const ByteString& key : keys) {
          RetainPtr<CPDF_Object> entry =
              map->GetMutableObjectFor(key.AsStringView());
          if (!entry || !entry->IsReference()) continue;
          uint32_t this_objnum = entry->AsReference()->GetRefObjNum();
          if (this_objnum == 0) continue;
          RetainPtr<const CPDF_Object> target =
              doc_->GetIndirectObject(this_objnum);
          if (!target) continue;
          uint64_t digest = DigestAnyObject(target.Get());
          if (digest == 0) continue;
          auto it = seen.find(digest);
          if (it == seen.end()) {
            seen[digest] = this_objnum;
          } else if (it->second != this_objnum) {
            map->SetNewFor<CPDF_Reference>(key, doc_, it->second);
            ++collapsed;
          }
        }
      }
    }
    return collapsed;
  }

  int DedupAllIndirectObjects() {
    const uint32_t last = doc_->GetLastObjNum();
    if (last == 0) return 0;

    std::unordered_map<uint64_t, uint32_t> seen;
    std::unordered_map<uint32_t, uint32_t> canonical;
    seen.reserve(last);

    for (uint32_t i = 1; i <= last; ++i) {
      RetainPtr<const CPDF_Object> obj = doc_->GetIndirectObject(i);
      if (!obj) continue;
      if (IsStructurallyUnsafeToMerge(obj.Get())) continue;
      uint64_t digest = DigestAnyObject(obj.Get());
      if (digest == 0) continue;
      auto it = seen.find(digest);
      if (it == seen.end()) {
        seen[digest] = i;
      } else if (it->second != i) {
        canonical[i] = it->second;
      }
    }
    if (canonical.empty()) return 0;

    for (uint32_t i = 1; i <= last; ++i) {
      if (canonical.count(i)) continue;
      RetainPtr<CPDF_Object> obj = doc_->GetOrParseIndirectObject(i);
      if (!obj) continue;
      if (IsStructurallyUnsafeToMerge(obj.Get())) {

      }
      RewriteRefsInObject(obj.Get(), canonical);
    }
    return static_cast<int>(canonical.size());
  }

  static bool IsStructurallyUnsafeToMerge(const CPDF_Object* obj) {
    if (!obj || !obj->IsDictionary()) return false;
    const CPDF_Dictionary* d = obj->AsDictionary();
    ByteString type = d->GetNameFor("Type");
    if (type == "Page" || type == "Pages" || type == "Catalog")
      return true;

    if (type == "Annot")
      return true;
    if (d->KeyExist("Rect") && d->KeyExist("Subtype"))
      return true;
    return false;
  }

  void RewriteRefsInObject(
      CPDF_Object* obj,
      const std::unordered_map<uint32_t, uint32_t>& canonical) {
    if (!obj) return;
    if (obj->IsDictionary()) {
      CPDF_Dictionary* dict = obj->AsMutableDictionary();
      std::vector<ByteString> keys;
      {
        CPDF_DictionaryLocker locker(dict);
        for (const auto& it : locker) keys.push_back(it.first);
      }
      for (const ByteString& k : keys) {
        RetainPtr<CPDF_Object> v =
            dict->GetMutableObjectFor(k.AsStringView());
        if (!v) continue;
        if (v->IsReference()) {
          uint32_t old = v->AsReference()->GetRefObjNum();
          auto it = canonical.find(old);
          if (it != canonical.end() && it->second != old) {
            dict->SetNewFor<CPDF_Reference>(k, doc_, it->second);
          }
        } else if (v->IsDictionary() || v->IsArray()) {
          RewriteRefsInObject(v.Get(), canonical);
        } else if (v->IsStream()) {

          RetainPtr<CPDF_Dictionary> sdict =
              v->AsMutableStream()->GetMutableDict();
          if (sdict) RewriteRefsInObject(sdict.Get(), canonical);
        }
      }
    } else if (obj->IsArray()) {
      CPDF_Array* arr = obj->AsMutableArray();
      for (size_t i = 0; i < arr->size(); ++i) {
        RetainPtr<CPDF_Object> v = arr->GetMutableObjectAt(i);
        if (!v) continue;
        if (v->IsReference()) {
          uint32_t old = v->AsReference()->GetRefObjNum();
          auto it = canonical.find(old);
          if (it != canonical.end() && it->second != old) {
            arr->SetNewAt<CPDF_Reference>(i, doc_, it->second);
          }
        } else if (v->IsDictionary() || v->IsArray()) {
          RewriteRefsInObject(v.Get(), canonical);
        } else if (v->IsStream()) {
          RetainPtr<CPDF_Dictionary> sdict =
              v->AsMutableStream()->GetMutableDict();
          if (sdict) RewriteRefsInObject(sdict.Get(), canonical);
        }
      }
    } else if (obj->IsStream()) {
      RetainPtr<CPDF_Dictionary> sdict =
          obj->AsMutableStream()->GetMutableDict();
      if (sdict) RewriteRefsInObject(sdict.Get(), canonical);
    }
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;
};

class HyperConvertToBitmap {
 public:
  HyperConvertToBitmap(CPDF_Document* doc,
                        FPDF_DOCUMENT fpdf_doc,
                        const CompressOptions& opts)
      : doc_(doc), fpdf_doc_(fpdf_doc), opts_(opts) {}

  bool HolderHasTextObject(CPDF_PageObjectHolder* holder, int depth) {
    if (!holder) return false;
    if (depth > 16) return true;
    const size_t obj_count = holder->GetActivePageObjectCount();
    for (size_t i = 0; i < obj_count; ++i) {
      CPDF_PageObject* obj = holder->GetPageObjectByIndex(i);
      if (!obj) continue;
      if (obj->IsText()) return true;
      if (obj->IsForm()) {
        CPDF_FormObject* form_obj = obj->AsForm();
        if (form_obj && HolderHasTextObject(form_obj->form(), depth + 1)) {
          return true;
        }
      }
    }
    return false;
  }

  bool DocumentHasTextLayer() {
    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RetainPtr<CPDF_Dictionary> page_dict = doc_->GetMutablePageDictionary(i);
      if (!page_dict) continue;
      if (!HyperIsLoadablePageDict(page_dict.Get())) continue;
      auto page = pdfium::MakeRetain<CPDF_Page>(doc_, page_dict);
      page->ParseContent();
      if (HolderHasTextObject(page.Get(), 0)) return true;
    }
    return false;
  }

  void Run() {
    if (!opts_.convert_to_bitmap || !fpdf_doc_) return;
    if (DocumentHasTextLayer()) {
      fprintf(stderr, "[hyper] rasterize skipped: document has a text layer\n");
      return;
    }

    int dpi = opts_.convert_to_bitmap_dpi;
    if (dpi < 36) dpi = 36;
    if (dpi > 600) dpi = 600;
    int quality = opts_.convert_to_bitmap_quality;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    const CPDF_Dictionary* cat = doc_->GetRoot();
    const bool has_acroform = cat && cat->KeyExist("AcroForm");
    if (has_acroform) {
      memset(&form_info_, 0, sizeof(form_info_));
      form_info_.version = 1;
      form_handle_ = FPDFDOC_InitFormFillEnvironment(fpdf_doc_, &form_info_);
    }

    const int page_count = doc_->GetPageCount();
    for (int i = 0; i < page_count; ++i) {
      RasterisePage(i, dpi, quality);
    }

    if (form_handle_) {
      FPDFDOC_ExitFormFillEnvironment(form_handle_);
      form_handle_ = nullptr;
    }

    if (has_acroform) {
      RetainPtr<CPDF_Dictionary> root = doc_->GetMutableRoot();
      if (root) {
        root->RemoveFor("AcroForm");
      }
    }
  }

 private:
  void RasterisePage(int page_index, int dpi, int quality) {
    FPDF_PAGE page = FPDF_LoadPage(fpdf_doc_, page_index);
    if (!page) return;

    FORM_OnAfterLoadPage(page, form_handle_);

    const double width_pts = FPDF_GetPageWidthF(page);
    const double height_pts = FPDF_GetPageHeightF(page);
    const int target_w =
        std::max(1, static_cast<int>(width_pts * dpi / 72.0));
    const int target_h =
        std::max(1, static_cast<int>(height_pts * dpi / 72.0));

    FPDF_BITMAP fpdf_bm = FPDFBitmap_Create(target_w, target_h, 0);
    if (!fpdf_bm) {
      FORM_OnBeforeClosePage(page, form_handle_);
      FPDF_ClosePage(page);
      return;
    }

    FPDFBitmap_FillRect(fpdf_bm, 0, 0, target_w, target_h, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(fpdf_bm, page, 0, 0, target_w, target_h,
                          0  ,
                          FPDF_LCD_TEXT);

    FPDF_FFLDraw(form_handle_, fpdf_bm, page, 0, 0, target_w, target_h,
                 0  , 0  );
    FORM_OnBeforeClosePage(page, form_handle_);
    FPDF_ClosePage(page);

    CFX_DIBitmap* raw_bm = CFXDIBitmapFromFPDFBitmap(fpdf_bm);
    RetainPtr<CFX_DIBitmap> bitmap = pdfium::WrapRetain(raw_bm);

    uint8_t* jpeg_buf = nullptr;
    size_t jpeg_len = 0;
    bool ok = false;
    UNSAFE_BUFFERS({
      ok = JpegModule::JpegEncode(bitmap, &jpeg_buf, &jpeg_len, quality,
                                   opts_.jpeg_subsample,
                                   opts_.jpeg_optimized_huffman != 0,
                                   opts_.jpeg_progressive != 0);
    });

    bitmap.Reset();
    FPDFBitmap_Destroy(fpdf_bm);
    if (!ok || !jpeg_buf || jpeg_len == 0) {
      if (jpeg_buf) FX_Free(jpeg_buf);
      return;
    }
    DataVector<uint8_t> jpeg_bytes(jpeg_buf,
                                    UNSAFE_TODO(jpeg_buf + jpeg_len));
    FX_Free(jpeg_buf);

    auto img_dict = doc_->New<CPDF_Dictionary>();
    img_dict->SetNewFor<CPDF_Name>("Type", "XObject");
    img_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    img_dict->SetNewFor<CPDF_Number>("Width", target_w);
    img_dict->SetNewFor<CPDF_Number>("Height", target_h);
    img_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
    img_dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceRGB");
    img_dict->SetNewFor<CPDF_Name>("Filter", "DCTDecode");

    auto img_stream = doc_->NewIndirect<CPDF_Stream>(
        std::move(jpeg_bytes), std::move(img_dict));
    if (!img_stream) return;
    uint32_t img_objnum = img_stream->GetObjNum();

    RetainPtr<CPDF_Dictionary> page_dict =
        doc_->GetMutablePageDictionary(page_index);
    if (!page_dict) return;

    page_dict->RemoveFor("Annots");
    page_dict->RemoveFor("Contents");
    page_dict->RemoveFor("Rotate");
    page_dict->RemoveFor("Group");
    page_dict->RemoveFor("Tabs");

    CFX_FloatRect page_box(0.0f, 0.0f,
                            static_cast<float>(width_pts),
                            static_cast<float>(height_pts));
    page_dict->SetRectFor("MediaBox", page_box);
    page_dict->SetRectFor("CropBox", page_box);
    page_dict->RemoveFor("BleedBox");
    page_dict->RemoveFor("TrimBox");
    page_dict->RemoveFor("ArtBox");

    auto resources = doc_->New<CPDF_Dictionary>();
    auto xobject = resources->SetNewFor<CPDF_Dictionary>("XObject");
    xobject->SetNewFor<CPDF_Reference>("OptImg", doc_, img_objnum);
    page_dict->SetFor("Resources", std::move(resources));

    char cs_buf[128];
    int cs_len = std::snprintf(
        cs_buf, sizeof(cs_buf), "q\n%.6f 0 0 %.6f 0 0 cm\n/OptImg Do\nQ\n",
        static_cast<float>(width_pts), static_cast<float>(height_pts));
    if (cs_len <= 0 || cs_len >= static_cast<int>(sizeof(cs_buf))) return;
    std::string cs_str(cs_buf, static_cast<size_t>(cs_len));

    auto cs_dict = doc_->New<CPDF_Dictionary>();
    DataVector<uint8_t> cs_bytes(
        reinterpret_cast<const uint8_t*>(cs_str.data()),
        reinterpret_cast<const uint8_t*>(cs_str.data()) + cs_str.size());
    auto cs_stream = doc_->NewIndirect<CPDF_Stream>(
        std::move(cs_bytes), std::move(cs_dict));
    if (!cs_stream) return;
    page_dict->SetNewFor<CPDF_Reference>("Contents", doc_,
                                          cs_stream->GetObjNum());
  }

  CPDF_Document* doc_;
  FPDF_DOCUMENT fpdf_doc_;
  const CompressOptions& opts_;

  FPDF_FORMFILLINFO form_info_ = {};
  FPDF_FORMHANDLE form_handle_ = nullptr;
};

static bool FieldTreeHasCompletedSig(const CPDF_Dictionary* field,
                                     const ByteString& inherited_ft,
                                     int depth) {
  if (!field || depth > 32)
    return false;
  const ByteString ft =
      field->KeyExist("FT") ? field->GetNameFor("FT") : inherited_ft;
  if (ft == "Sig") {
    RetainPtr<const CPDF_Object> v = field->GetDirectObjectFor("V");
    if (v && !v->IsNull()) {
      RetainPtr<const CPDF_Dictionary> vd = v->GetDict();
      if (!vd)
        return true;
      if (vd->KeyExist("ByteRange") || vd->KeyExist("Contents"))
        return true;

    }
  }
  RetainPtr<const CPDF_Array> kids = field->GetArrayFor("Kids");
  if (kids) {
    CPDF_ArrayLocker locker(std::move(kids));
    for (auto& kid : locker) {
      RetainPtr<const CPDF_Dictionary> kd = kid->GetDict();
      if (kd && FieldTreeHasCompletedSig(kd.Get(), ft, depth + 1))
        return true;
    }
  }
  return false;
}

static bool HyperStreamIsPlainFlateOrRaw(const CPDF_Dictionary* d) {
  if (!d)
    return true;
  if (d->KeyExist("F") || d->KeyExist("DecodeParms") || d->KeyExist("DP"))
    return false;
  RetainPtr<const CPDF_Object> f = d->GetDirectObjectFor("Filter");
  if (!f)
    return true;
  if (f->IsName()) {
    const ByteString n = f->GetString();
    return n == "FlateDecode" || n == "Fl";
  }
  const CPDF_Array* arr = f->AsArray();
  if (!arr || arr->size() != 1)
    return false;
  RetainPtr<const CPDF_Object> f0 = arr->GetDirectObjectAt(0);
  if (!f0 || !f0->IsName())
    return false;
  const ByteString n = f0->GetString();
  return n == "FlateDecode" || n == "Fl";
}

static void HyperMarkPdf20WithBrotliExtension(CPDF_Document* doc) {
  RetainPtr<CPDF_Dictionary> root = doc->GetMutableRoot();
  if (!root)
    return;
  root->SetNewFor<CPDF_Name>("Version", "2.0");
  RetainPtr<CPDF_Dictionary> ext = root->GetMutableDictFor("Extensions");
  if (!ext)
    ext = root->SetNewFor<CPDF_Dictionary>("Extensions");
  RetainPtr<CPDF_Dictionary> pdfa = ext->GetMutableDictFor("PDFa");
  if (!pdfa)
    pdfa = ext->SetNewFor<CPDF_Dictionary>("PDFa");
  pdfa->SetNewFor<CPDF_Name>("Type", "DeveloperExtensions");
  pdfa->SetNewFor<CPDF_Name>("BaseVersion", "2.0");
  pdfa->SetNewFor<CPDF_Number>("ExtensionLevel", 1);
  pdfa->SetNewFor<CPDF_String>("ExtensionRevision", "2026");
  pdfa->SetNewFor<CPDF_String>("URL",
                               "https://pdfa.org/resource/extension-brotli");
}

static int HyperBrotliRecodeStreams(CPDF_Document* doc,
                                    const CompressOptions& opts) {
  if (!doc || opts.stream_codec != 1)
    return 0;
  int quality = opts.brotli_quality;
  if (quality < 1) quality = 1;
  if (quality > 11) quality = 11;

  int converted = 0;
  const uint32_t last = doc->GetLastObjNum();
  for (uint32_t i = 1; i <= last; ++i) {
    RetainPtr<CPDF_Object> obj = doc->GetOrParseIndirectObject(i);
    if (!obj || !obj->IsStream())
      continue;
    CPDF_Stream* stream = obj->AsMutableStream();
    RetainPtr<CPDF_Dictionary> d = stream->GetMutableDict();
    if (d) {
      const ByteString type = d->GetNameFor("Type");
      if (type == "XRef" || type == "ObjStm")
        continue;
      if (d->GetNameFor("Subtype") == "Image")
        continue;
    }
    if (!HyperStreamIsPlainFlateOrRaw(d.Get()))
      continue;

    auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(stream));
    acc->LoadAllDataFiltered();
    pdfium::span<const uint8_t> decoded = acc->GetSpan();
    if (decoded.size() < 256)
      continue;

    size_t bound = BrotliEncoderMaxCompressedSize(decoded.size());
    if (bound == 0)
      continue;
    DataVector<uint8_t> brotli(bound);
    size_t out_size = bound;
    if (!BrotliEncoderCompress(quality, BROTLI_MAX_WINDOW_BITS,
                               BROTLI_MODE_GENERIC, decoded.size(),
                               decoded.data(), &out_size, brotli.data())) {
      continue;
    }
    brotli.resize(out_size);

    size_t best_flate = stream->GetRawSize();
    DataVector<uint8_t> reflate = FlateModule::Encode(decoded);
    if (!reflate.empty() && reflate.size() < best_flate)
      best_flate = reflate.size();
    if (out_size == 0 || out_size >= best_flate)
      continue;

    const int blen = static_cast<int>(out_size);
    stream->TakeData(std::move(brotli));
    RetainPtr<CPDF_Dictionary> md = stream->GetMutableDict();
    if (!md)
      continue;
    md->SetNewFor<CPDF_Name>("Filter", "BrotliDecode");
    md->RemoveFor("DecodeParms");
    md->RemoveFor("DP");
    md->SetNewFor<CPDF_Number>("Length", blen);
    ++converted;
  }

  if (converted > 0)
    HyperMarkPdf20WithBrotliExtension(doc);
  return converted;
}

bool HyperDocIsSigned(CPDF_Document* doc) {
  if (!doc)
    return false;
  const CPDF_Dictionary* root = doc->GetRoot();
  if (!root)
    return false;

  RetainPtr<const CPDF_Dictionary> perms = root->GetDictFor("Perms");
  if (perms && perms->size() > 0)
    return true;
  RetainPtr<const CPDF_Dictionary> acro_form = root->GetDictFor("AcroForm");
  if (!acro_form)
    return false;
  RetainPtr<const CPDF_Array> fields = acro_form->GetArrayFor("Fields");
  if (!fields)
    return false;
  CPDF_ArrayLocker locker(std::move(fields));
  for (auto& field : locker) {
    RetainPtr<const CPDF_Dictionary> field_dict = field->GetDict();
    if (field_dict &&
        FieldTreeHasCompletedSig(field_dict.Get(), ByteString(), 0))
      return true;
  }
  return false;
}

void HyperCoalescePageContents(CPDF_Document* doc) {
  if (!doc)
    return;
  const int page_count = doc->GetPageCount();
  for (int i = 0; i < page_count; ++i) {
    RetainPtr<CPDF_Dictionary> page_dict = doc->GetMutablePageDictionary(i);
    if (!page_dict)
      continue;
    RetainPtr<CPDF_Array> arr = page_dict->GetMutableArrayFor("Contents");
    if (!arr || arr->size() <= 1)
      continue;
    fxcrt::ostringstream merged;
    bool ok = true;
    for (size_t j = 0; j < arr->size(); ++j) {
      RetainPtr<const CPDF_Object> obj = arr->GetDirectObjectAt(j);
      const CPDF_Stream* s = obj ? obj->AsStream() : nullptr;
      if (!s) {
        ok = false;
        break;
      }
      auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(s));
      acc->LoadAllDataFiltered();
      pdfium::span<const uint8_t> span = acc->GetSpan();
      if (!span.empty()) {
        merged.write(reinterpret_cast<const char*>(span.data()),
                     static_cast<std::streamsize>(span.size()));
      }
      merged << "\n";
    }
    if (!ok)
      continue;

    const fxcrt::string merged_str = merged.str();
    DataVector<uint8_t> flate = FlateModule::Encode(pdfium::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(merged_str.data()),
        merged_str.size()));
    auto cdict = doc->New<CPDF_Dictionary>();
    cdict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    auto new_stream =
        doc->NewIndirect<CPDF_Stream>(std::move(flate), std::move(cdict));
    page_dict->SetNewFor<CPDF_Reference>("Contents", doc,
                                          new_stream->GetObjNum());
  }
}

int HyperFlateUncompressedStreams(CPDF_Document* doc, bool pdfa_mode) {
  if (!doc)
    return 0;
  int recompressed = 0;
  const uint32_t last = doc->GetLastObjNum();
  for (uint32_t i = 1; i <= last; ++i) {
    RetainPtr<CPDF_Object> obj = doc->GetOrParseIndirectObject(i);
    if (!obj || !obj->IsStream())
      continue;
    CPDF_Stream* s = obj->AsMutableStream();
    RetainPtr<CPDF_Dictionary> d = s->GetMutableDict();
    if (!d || d->KeyExist("Filter"))
      continue;
    const ByteString type = d->GetNameFor("Type");
    if (type == "XRef" || type == "ObjStm")
      continue;
    if (pdfa_mode && type == "Metadata")
      continue;
    auto acc =
        pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(obj->AsStream()));
    acc->LoadAllDataRaw();
    pdfium::span<const uint8_t> raw = acc->GetSpan();
    if (raw.size() < 256)
      continue;
    DataVector<uint8_t> flate = FlateModule::Encode(raw);
    if (flate.empty() || flate.size() >= raw.size())
      continue;
    const int flen = static_cast<int>(flate.size());
    s->TakeData(std::move(flate));
    d->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    d->SetNewFor<CPDF_Number>("Length", flen);
    ++recompressed;
  }
  return recompressed;
}

class HyperIccFlattener {
 public:
  HyperIccFlattener(CPDF_Document* doc, const CompressOptions& opts)
      : doc_(doc), opts_(opts) {}

  int Run() {
    if (!opts_.flatten_icc)
      return 0;
    min_profile_bytes_ = opts_.flatten_icc > 1
                             ? static_cast<size_t>(opts_.flatten_icc)
                             : kDefaultMinProfileBytes;
    flattened_ = 0;
    const int page_count = doc_->GetPageCount();
    for (int p = 0; p < page_count; ++p) {
      RetainPtr<CPDF_Dictionary> page = doc_->GetMutablePageDictionary(p);
      if (!page)
        continue;
      RetainPtr<CPDF_Dictionary> resources = page->GetMutableDictFor("Resources");
      VisitResources(resources);
      FlattenGroupCS(page.Get());
      RetainPtr<CPDF_Array> annots = page->GetMutableArrayFor("Annots");
      VisitAnnotAppearances(annots);
    }
    return flattened_;
  }

 private:

  static constexpr size_t kDefaultMinProfileBytes = 2 * 1024;

  RetainPtr<CPDF_Object> Resolve(RetainPtr<CPDF_Object> o) {
    if (o && o->IsReference())
      return doc_->GetOrParseIndirectObject(o->AsReference()->GetRefObjNum());
    return o;
  }

  const char* DeviceNameForLargeIcc(const CPDF_Object* cs) {
    const CPDF_Array* arr = cs ? cs->AsArray() : nullptr;
    if (!arr || arr->size() < 2)
      return nullptr;
    RetainPtr<const CPDF_Object> head = arr->GetDirectObjectAt(0);
    if (!head || !head->IsName() || head->GetString() != "ICCBased")
      return nullptr;
    RetainPtr<const CPDF_Object> prof = arr->GetDirectObjectAt(1);
    const CPDF_Stream* st = prof ? prof->AsStream() : nullptr;
    if (!st)
      return nullptr;
    if (st->GetRawSize() < min_profile_bytes_)
      return nullptr;
    RetainPtr<const CPDF_Dictionary> sd = st->GetDict();
    switch (sd ? sd->GetIntegerFor("N") : 0) {
      case 1:  return "DeviceGray";
      case 3:  return "DeviceRGB";
      case 4:  return "DeviceCMYK";
      default: return nullptr;
    }
  }

  void FlattenSlot(CPDF_Dictionary* dict, const ByteString& key) {
    if (!dict)
      return;
    RetainPtr<CPDF_Object> resolved =
        Resolve(dict->GetMutableObjectFor(key.AsStringView()));
    if (!resolved)
      return;
    if (const char* dev = DeviceNameForLargeIcc(resolved.Get())) {
      dict->SetNewFor<CPDF_Name>(key, dev);
      ++flattened_;
      return;
    }
    FlattenIndexedBase(resolved.Get());
  }

  void FlattenIndexedBase(CPDF_Object* cs) {
    CPDF_Array* arr = cs ? cs->AsMutableArray() : nullptr;
    if (!arr || arr->size() < 4)
      return;
    RetainPtr<const CPDF_Object> head = arr->GetDirectObjectAt(0);
    if (!head || !head->IsName())
      return;
    const ByteString fam = head->GetString();
    if (fam != "Indexed" && fam != "I")
      return;
    RetainPtr<const CPDF_Object> base = arr->GetDirectObjectAt(1);
    if (const char* dev = DeviceNameForLargeIcc(base.Get())) {
      arr->SetNewAt<CPDF_Name>(1, dev);
      ++flattened_;
    }
  }

  void FlattenGroupCS(CPDF_Dictionary* holder) {
    if (!holder)
      return;
    RetainPtr<CPDF_Dictionary> grp = holder->GetMutableDictFor("Group");
    if (grp)
      FlattenSlot(grp.Get(), "CS");
  }

  void VisitResources(RetainPtr<CPDF_Dictionary> resources) {
    if (!resources)
      return;
    const uint32_t objnum = resources->GetObjNum();
    if (objnum != 0 && !visited_.insert(objnum).second)
      return;

    RetainPtr<CPDF_Dictionary> cs = resources->GetMutableDictFor("ColorSpace");
    if (cs) {
      for (const ByteString& k : DictKeys(cs.Get()))
        FlattenSlot(cs.Get(), k);
    }

    RetainPtr<CPDF_Dictionary> xo = resources->GetMutableDictFor("XObject");
    if (xo) {
      for (const ByteString& k : DictKeys(xo.Get())) {
        RetainPtr<CPDF_Object> obj = Resolve(xo->GetMutableObjectFor(k.AsStringView()));
        CPDF_Stream* st = obj ? obj->AsMutableStream() : nullptr;
        RetainPtr<CPDF_Dictionary> sd = st ? st->GetMutableDict() : nullptr;
        if (!sd)
          continue;
        const ByteString sub = sd->GetNameFor("Subtype");
        if (sub == "Image") {

          FlattenSlot(sd.Get(), "ColorSpace");
          FlattenSlot(sd.Get(), "CS");
        } else if (sub == "Form") {
          FlattenGroupCS(sd.Get());
          VisitResources(sd->GetMutableDictFor("Resources"));
        }
      }
    }

    RetainPtr<CPDF_Dictionary> pat = resources->GetMutableDictFor("Pattern");
    if (pat) {
      for (const ByteString& k : DictKeys(pat.Get())) {
        RetainPtr<CPDF_Object> obj = Resolve(pat->GetMutableObjectFor(k.AsStringView()));
        if (!obj)
          continue;

        CPDF_Dictionary* pd = nullptr;
        if (CPDF_Stream* st = obj->AsMutableStream())
          pd = st->GetMutableDict().Get();
        else
          pd = obj->AsMutableDictionary();
        if (pd)
          VisitResources(pd->GetMutableDictFor("Resources"));
      }
    }

    RetainPtr<CPDF_Dictionary> sh = resources->GetMutableDictFor("Shading");
    if (sh) {
      for (const ByteString& k : DictKeys(sh.Get())) {
        RetainPtr<CPDF_Object> obj = Resolve(sh->GetMutableObjectFor(k.AsStringView()));
        if (!obj)
          continue;
        CPDF_Dictionary* shd = nullptr;
        if (CPDF_Stream* st = obj->AsMutableStream())
          shd = st->GetMutableDict().Get();
        else
          shd = obj->AsMutableDictionary();
        if (shd)
          FlattenSlot(shd, "ColorSpace");
      }
    }
  }

  void VisitAnnotAppearances(RetainPtr<CPDF_Array> annots) {
    if (!annots)
      return;
    for (size_t i = 0; i < annots->size(); ++i) {
      RetainPtr<const CPDF_Object> ao = annots->GetObjectAt(i);
      if (!ao || !ao->IsReference())
        continue;
      RetainPtr<CPDF_Object> ad =
          doc_->GetOrParseIndirectObject(ao->AsReference()->GetRefObjNum());
      CPDF_Dictionary* adict = ad ? ad->AsMutableDictionary() : nullptr;
      if (!adict)
        continue;
      RetainPtr<CPDF_Dictionary> ap = adict->GetMutableDictFor("AP");
      if (!ap)
        continue;

      for (const ByteString& slot : DictKeys(ap.Get())) {
        RetainPtr<CPDF_Object> ent = Resolve(ap->GetMutableObjectFor(slot.AsStringView()));
        if (!ent)
          continue;
        if (CPDF_Stream* st = ent->AsMutableStream()) {
          RetainPtr<CPDF_Dictionary> sd = st->GetMutableDict();
          if (sd)
            VisitResources(sd->GetMutableDictFor("Resources"));
        } else if (CPDF_Dictionary* sub = ent->AsMutableDictionary()) {
          for (const ByteString& sk : DictKeys(sub)) {
            RetainPtr<CPDF_Object> s2 = Resolve(sub->GetMutableObjectFor(sk.AsStringView()));
            CPDF_Stream* st2 = s2 ? s2->AsMutableStream() : nullptr;
            RetainPtr<CPDF_Dictionary> sd2 = st2 ? st2->GetMutableDict() : nullptr;
            if (sd2)
              VisitResources(sd2->GetMutableDictFor("Resources"));
          }
        }
      }
    }
  }

  static std::vector<ByteString> DictKeys(const CPDF_Dictionary* d) {
    std::vector<ByteString> keys;
    if (!d)
      return keys;
    CPDF_DictionaryLocker locker(d);
    for (const auto& it : locker)
      keys.push_back(it.first);
    return keys;
  }

  CPDF_Document* doc_;
  const CompressOptions& opts_;
  size_t min_profile_bytes_ = kDefaultMinProfileBytes;
  int flattened_ = 0;
  std::unordered_set<uint32_t> visited_;
};

}

extern "C" {

FPDF_EXPORT int FPDF_CALLCONV
HyperCompress_DocIsSigned(FPDF_DOCUMENT document) {
  return HyperDocIsSigned(CPDFDocumentFromFPDFDocument(document)) ? 1 : 0;
}

FPDF_EXPORT FPDF_COMPRESS_OPTIONS FPDF_CALLCONV
HyperCompress_CreateOptions(void) {
  return reinterpret_cast<FPDF_COMPRESS_OPTIONS>(new CompressOptions());
}

FPDF_EXPORT void FPDF_CALLCONV
HyperCompress_SetOption(FPDF_COMPRESS_OPTIONS h, int option, int value) {
  CompressOptions* opts = GetOpts(h);
  if (!opts) return;
  switch (option) {
    case HYPERC_OPT_IMAGE_MAX_DPI:        opts->image_max_dpi = value; break;
    case HYPERC_OPT_IMAGE_THRESHOLD_DPI:  opts->image_threshold_dpi = value; break;
    case HYPERC_OPT_IMAGE_QUALITY:        opts->image_quality = value; break;
    case HYPERC_OPT_IMAGE_ENCODING:       opts->image_encoding = value; break;
    case HYPERC_OPT_IMAGE_GRAYSCALE:      opts->image_grayscale = value; break;
    case HYPERC_OPT_IMAGE_COLOR_TARGET:   opts->image_color_target = value; break;
    case HYPERC_OPT_IMAGE_RESAMPLE_QUALITY:
      opts->image_resample_quality = value; break;
    case HYPERC_OPT_JPEG_SUBSAMPLE:       opts->jpeg_subsample = value; break;
    case HYPERC_OPT_JPEG_OPTIM_HUFFMAN:   opts->jpeg_optimized_huffman = value; break;
    case HYPERC_OPT_JPEG_PROGRESSIVE:     opts->jpeg_progressive = value; break;
    case HYPERC_OPT_CLIP_IMAGES:          opts->clip_images = value; break;
    case HYPERC_OPT_REDUCE_COLOR_COMPLEXITY:
      opts->reduce_color_complexity = value; break;
    case HYPERC_OPT_IMAGE_INDEX_MIN_DPI:  opts->image_index_min_dpi = value; break;
    case HYPERC_OPT_IMAGE_COLOR_MAX_DPI:  opts->image_color_max_dpi = value; break;
    case HYPERC_OPT_IMAGE_GRAY_MAX_DPI:   opts->image_gray_max_dpi = value; break;
    case HYPERC_OPT_IMAGE_MONO_MAX_DPI:   opts->image_mono_max_dpi = value; break;
    case HYPERC_OPT_IMAGE_LOSSY_INDEX:    opts->image_lossy_index = value; break;
    case HYPERC_OPT_IMAGE_PREFER_JPX:     opts->image_prefer_jpx = value; break;
    case HYPERC_OPT_FONT_SUBSET:          opts->font_subset = value; break;
    case HYPERC_OPT_FONT_REMOVE_STANDARD: opts->font_remove_standard = value; break;
    case HYPERC_OPT_UNEMBED_ALIASED_FONTS: opts->unembed_aliased_fonts = value; break;
    case HYPERC_OPT_FONT_MERGE:           opts->font_merge = value; break;
    case HYPERC_OPT_FONT_DEDUP_DICTS:     opts->font_dedup_dicts = value; break;
    case HYPERC_OPT_DEDUP_OBJECTS:        opts->dedup_objects = value; break;
    case HYPERC_OPT_OPTIMIZE_RESOURCES:   opts->optimize_resources = value; break;
    case HYPERC_OPT_FLATTEN_ICC:          opts->flatten_icc = value; break;
    case HYPERC_OPT_CONVERT_TO_BITMAP:    opts->convert_to_bitmap = value; break;
    case HYPERC_OPT_CONVERT_TO_BITMAP_DPI:
      opts->convert_to_bitmap_dpi = value; break;
    case HYPERC_OPT_CONVERT_TO_BITMAP_QUALITY:
      opts->convert_to_bitmap_quality = value; break;
    case HYPERC_OPT_DISCARD_MASK:
      opts->discard_mask = static_cast<uint32_t>(value); break;
    case HYPERC_OPT_MRC_MODE:             opts->mrc_mode = value; break;
    case HYPERC_OPT_MRC_SELECTOR_DPI:     opts->mrc_selector_dpi = value; break;
    case HYPERC_OPT_MRC_BG_DPI:           opts->mrc_bg_dpi = value; break;
    case HYPERC_OPT_MRC_BG_QUALITY:       opts->mrc_bg_quality = value; break;
    case HYPERC_OPT_MRC_FG_QUALITY:       opts->mrc_fg_quality = value; break;
    case HYPERC_OPT_RECOMPRESS_CONTENT_STREAMS:
      opts->recompress_content_streams = value; break;
    case HYPERC_OPT_STREAM_CODEC:
      opts->stream_codec = value; break;
    case HYPERC_OPT_BROTLI_QUALITY:
      opts->brotli_quality = value; break;
    case HYPERC_OPT_PDFA_MODE:            opts->pdfa_mode = value; break;
    default: break;
  }
}

FPDF_EXPORT int FPDF_CALLCONV
HyperCompress_GetOption(FPDF_COMPRESS_OPTIONS h, int option) {
  CompressOptions* opts = GetOpts(h);
  if (!opts) return 0;
  switch (option) {
    case HYPERC_OPT_IMAGE_MAX_DPI:        return opts->image_max_dpi;
    case HYPERC_OPT_IMAGE_THRESHOLD_DPI:  return opts->image_threshold_dpi;
    case HYPERC_OPT_IMAGE_QUALITY:        return opts->image_quality;
    case HYPERC_OPT_IMAGE_ENCODING:       return opts->image_encoding;
    case HYPERC_OPT_IMAGE_GRAYSCALE:      return opts->image_grayscale;
    case HYPERC_OPT_IMAGE_COLOR_TARGET:   return opts->image_color_target;
    case HYPERC_OPT_IMAGE_RESAMPLE_QUALITY:
      return opts->image_resample_quality;
    case HYPERC_OPT_JPEG_SUBSAMPLE:       return opts->jpeg_subsample;
    case HYPERC_OPT_JPEG_OPTIM_HUFFMAN:   return opts->jpeg_optimized_huffman;
    case HYPERC_OPT_JPEG_PROGRESSIVE:     return opts->jpeg_progressive;
    case HYPERC_OPT_CLIP_IMAGES:          return opts->clip_images;
    case HYPERC_OPT_REDUCE_COLOR_COMPLEXITY:
      return opts->reduce_color_complexity;
    case HYPERC_OPT_IMAGE_INDEX_MIN_DPI:  return opts->image_index_min_dpi;
    case HYPERC_OPT_IMAGE_COLOR_MAX_DPI:  return opts->image_color_max_dpi;
    case HYPERC_OPT_IMAGE_GRAY_MAX_DPI:   return opts->image_gray_max_dpi;
    case HYPERC_OPT_IMAGE_MONO_MAX_DPI:   return opts->image_mono_max_dpi;
    case HYPERC_OPT_IMAGE_LOSSY_INDEX:    return opts->image_lossy_index;
    case HYPERC_OPT_IMAGE_PREFER_JPX:     return opts->image_prefer_jpx;
    case HYPERC_OPT_FONT_SUBSET:          return opts->font_subset;
    case HYPERC_OPT_FONT_REMOVE_STANDARD: return opts->font_remove_standard;
    case HYPERC_OPT_UNEMBED_ALIASED_FONTS: return opts->unembed_aliased_fonts;
    case HYPERC_OPT_FONT_MERGE:           return opts->font_merge;
    case HYPERC_OPT_FONT_DEDUP_DICTS:     return opts->font_dedup_dicts;
    case HYPERC_OPT_DEDUP_OBJECTS:        return opts->dedup_objects;
    case HYPERC_OPT_OPTIMIZE_RESOURCES:   return opts->optimize_resources;
    case HYPERC_OPT_FLATTEN_ICC:          return opts->flatten_icc;
    case HYPERC_OPT_CONVERT_TO_BITMAP:    return opts->convert_to_bitmap;
    case HYPERC_OPT_CONVERT_TO_BITMAP_DPI:
      return opts->convert_to_bitmap_dpi;
    case HYPERC_OPT_CONVERT_TO_BITMAP_QUALITY:
      return opts->convert_to_bitmap_quality;
    case HYPERC_OPT_DISCARD_MASK:
      return static_cast<int>(opts->discard_mask);
    case HYPERC_OPT_MRC_MODE:             return opts->mrc_mode;
    case HYPERC_OPT_MRC_SELECTOR_DPI:     return opts->mrc_selector_dpi;
    case HYPERC_OPT_MRC_BG_DPI:           return opts->mrc_bg_dpi;
    case HYPERC_OPT_MRC_BG_QUALITY:       return opts->mrc_bg_quality;
    case HYPERC_OPT_MRC_FG_QUALITY:       return opts->mrc_fg_quality;
    case HYPERC_OPT_RECOMPRESS_CONTENT_STREAMS:
      return opts->recompress_content_streams;
    case HYPERC_OPT_STREAM_CODEC:
      return opts->stream_codec;
    case HYPERC_OPT_BROTLI_QUALITY:
      return opts->brotli_quality;
    case HYPERC_OPT_PDFA_MODE:
      return opts->pdfa_mode;
    default: return 0;
  }
}

FPDF_EXPORT void FPDF_CALLCONV
HyperCompress_CloseOptions(FPDF_COMPRESS_OPTIONS h) {
  delete GetOpts(h);
}

FPDF_EXPORT int FPDF_CALLCONV
HyperCompress_Execute(FPDF_DOCUMENT document, FPDF_COMPRESS_OPTIONS h) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) return 0;
  CompressOptions* opts = GetOpts(h);
  if (!opts) return 0;

  if (HyperDocIsSigned(doc))
    return HYPERC_EXEC_SKIPPED_SIGNED;

  HyperCoalescePageContents(doc);

  HyperConvertToBitmap rasterise(doc, document, *opts);
  rasterise.Run();

  HyperAnnotationFlatten flatten(doc, document, *opts);
  flatten.Run();

  HyperCatalogDiscard discard(doc, *opts);
  discard.Run();

  const std::unordered_set<int> no_mrc_pages;
  HyperImageRewrite image_pass(doc, *opts, no_mrc_pages);
  image_pass.Run();

  HyperMrcEncoder mrc(doc, *opts);
  mrc.Run();

  HyperFontOptimize font_pass(doc, *opts);
  font_pass.Run();

  HyperIccFlattener icc_pass(doc, *opts);
  icc_pass.Run();

  HyperFlateUncompressedStreams(doc, (opts->pdfa_mode & 1) != 0);
  if ((opts->pdfa_mode & 1) == 0)
    HyperBrotliRecodeStreams(doc, *opts);

  HyperObjectDedup dedup_pass(doc, *opts);
  dedup_pass.Run();

  return 1;
}

}
