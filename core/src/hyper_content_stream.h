#ifndef FPDFSDK_HYPER_CONTENT_STREAM_H_
#define FPDFSDK_HYPER_CONTENT_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hyper_cs {

inline bool IsWhitespace(uint8_t c) {
  return c == 0x00 || c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D ||
         c == 0x20;
}
inline bool IsDelimiter(uint8_t c) {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
         c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

enum class TokenType : uint8_t {
  kNumber,
  kName,
  kLitString,
  kHexString,
  kArrayOpen,
  kArrayClose,
  kDictOpen,
  kDictClose,
  kBraceOpen,
  kBraceClose,
  kKeyword,
};

struct Token {
  TokenType type;
  size_t begin;
  size_t end;
};

class Lexer {
 public:
  Lexer(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool Next(Token* tok);
  bool ok() const { return ok_; }
  size_t pos() const { return pos_; }
  void set_pos(size_t p) { pos_ = p; }
  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  std::string_view Text(const Token& t) const {
    return std::string_view(reinterpret_cast<const char*>(data_) + t.begin,
                            t.end - t.begin);
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  bool ok_ = true;
};

bool IsNumberText(std::string_view tok);

std::string CanonicalNumber(std::string_view tok);

std::string RoundNumber(std::string_view tok,
                        int sig_digits,
                        int min_decimals,
                        int max_decimals);

double ParseNumber(std::string_view tok);

std::string FormatDouble(double v);

struct CompactOptions {

  int precision = 0;

  const std::map<std::string, int, std::less<>>* device_spaces = nullptr;

  int convert_ncomp = 0;

  std::function<void(int n_in, const double* in, double* out)> convert;

  int* converted = nullptr;
};

bool CompactContent(const uint8_t* data,
                    size_t size,
                    const CompactOptions& opts,
                    std::string* out);

struct DoPatch {
  std::string name;
  int occurrence = 0;
  std::string new_name;
  bool has_matrix = false;
  double m[6] = {1, 0, 0, 1, 0, 0};
};

struct PatchResult {
  std::string content;

  std::map<std::string, int, std::less<>> uses;
  int applied = 0;
};

bool PatchDoSites(const uint8_t* data,
                  size_t size,
                  const std::vector<DoPatch>& patches,
                  PatchResult* result);

struct DoEvent {
  std::string_view name;
  int occurrence;
  double ctm[6];
  bool in_text;
};
bool WalkDoOperators(const uint8_t* data,
                     size_t size,
                     const std::function<void(const DoEvent&)>& on_do,
                     bool* has_inline_image);

bool Deflate(const uint8_t* in, size_t size, int level,
             std::vector<uint8_t>* out);

bool Reflate(const uint8_t* in, size_t size, int level,
             std::vector<uint8_t>* out);

bool DeflateParallel(const uint8_t* in, size_t size, int level, int threads,
                     std::vector<uint8_t>* out);

bool Inflate(const uint8_t* in, size_t size, size_t max_out,
             std::vector<uint8_t>* out);

int ZlibHeaderLevel(const uint8_t* in, size_t size);

int ResolveThreadCount(int requested);

void ParallelFor(size_t n, int threads, const std::function<void(size_t)>& fn);

}

#endif
