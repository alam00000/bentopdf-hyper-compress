#include "fpdfsdk/hyper_content_stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>

#if defined(USE_SYSTEM_ZLIB)
#include <zlib.h>
#else
#include "third_party/zlib/zlib.h"
#endif

namespace hyper_cs {

namespace {

bool IsRegular(uint8_t c) {
  return !IsWhitespace(c) && !IsDelimiter(c);
}

bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool IsNumericLike(std::string_view kw) {
  if (kw.empty()) return false;
  for (char c : kw)
    if (!IsDigit(c) && c != '+' && c != '-' && c != '.') return false;
  return true;
}

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string DecodeName(std::string_view tok) {
  std::string out;
  out.reserve(tok.size());
  for (size_t i = 1; i < tok.size(); ++i) {
    if (tok[i] == '#' && i + 2 < tok.size()) {
      const int hi = HexVal(tok[i + 1]);
      const int lo = HexVal(tok[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(tok[i]);
  }
  return out;
}

struct SplitNumber {
  bool neg = false;
  std::string_view int_part;
  std::string_view frac_part;
};

SplitNumber Split(std::string_view tok) {
  SplitNumber s;
  size_t i = 0;
  if (!tok.empty() && (tok[0] == '+' || tok[0] == '-')) {
    s.neg = tok[0] == '-';
    i = 1;
  }
  std::string_view rest = tok.substr(i);
  const size_t dot = rest.find('.');
  s.int_part = dot == std::string_view::npos ? rest : rest.substr(0, dot);
  s.frac_part =
      dot == std::string_view::npos ? std::string_view() : rest.substr(dot + 1);
  while (!s.int_part.empty() && s.int_part.front() == '0')
    s.int_part.remove_prefix(1);
  return s;
}

std::string Assemble(bool neg, std::string_view int_part,
                     std::string_view frac_part) {
  while (!int_part.empty() && int_part.front() == '0')
    int_part.remove_prefix(1);
  while (!frac_part.empty() && frac_part.back() == '0')
    frac_part.remove_suffix(1);
  if (int_part.empty() && frac_part.empty())
    return "0";
  std::string out;
  out.reserve(int_part.size() + frac_part.size() + 2);
  if (neg) out.push_back('-');
  out.append(int_part.data(), int_part.size());
  if (!frac_part.empty()) {
    out.push_back('.');
    out.append(frac_part.data(), frac_part.size());
  }
  return out;
}

void Concat(const double a[6], const double b[6], double out[6]) {
  double r[6];
  r[0] = a[0] * b[0] + a[1] * b[2];
  r[1] = a[0] * b[1] + a[1] * b[3];
  r[2] = a[2] * b[0] + a[3] * b[2];
  r[3] = a[2] * b[1] + a[3] * b[3];
  r[4] = a[4] * b[0] + a[5] * b[2] + b[4];
  r[5] = a[4] * b[1] + a[5] * b[3] + b[5];
  std::memcpy(out, r, sizeof(r));
}

bool IsColorOperator(std::string_view op) {
  return op == "g" || op == "G" || op == "rg" || op == "RG" || op == "k" ||
         op == "K" || op == "sc" || op == "SC" || op == "scn" || op == "SCN";
}

bool IsPathPaint(std::string_view op) {
  return op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" ||
         op == "B" || op == "B*" || op == "b" || op == "b*" || op == "n";
}

enum class OpndKind : uint8_t { kNumber, kName, kString, kArray, kDict, kOther };

struct Operand {
  OpndKind kind;
  std::string text;
};

bool EndsTight(const std::string& s) {
  if (s.empty()) return false;
  const char c = s.back();
  return c == ')' || c == '>' || c == ']';
}

bool StartsTight(const std::string& s) {
  if (s.empty()) return false;
  const char c = s.front();
  return c == '(' || c == '<' || c == '[';
}

void AppendJoined(std::string* out, const std::string& piece) {
  if (!out->empty() && !EndsTight(*out) && !StartsTight(piece) &&
      out->back() != '\n' && out->back() != '[')
    out->push_back(' ');
  out->append(piece);
}

bool ReadArray(Lexer& lx,
               const std::function<std::string(std::string_view)>& fmt,
               std::string* out);

bool ReadDictRaw(Lexer& lx, size_t open_begin, std::string* out) {
  int depth = 1;
  Token t;
  while (lx.Next(&t)) {
    if (t.type == TokenType::kDictOpen) {
      ++depth;
    } else if (t.type == TokenType::kDictClose) {
      if (--depth == 0) {
        out->assign(reinterpret_cast<const char*>(lx.data()) + open_begin,
                    t.end - open_begin);
        return true;
      }
    } else if (t.type == TokenType::kKeyword) {
      const std::string_view kw = lx.Text(t);
      if (kw == "BI" || kw == "ID" || kw == "EI") return false;
    }
  }
  return false;
}

bool ReadArray(Lexer& lx,
               const std::function<std::string(std::string_view)>& fmt,
               std::string* out) {
  out->assign("[");
  Token t;
  while (lx.Next(&t)) {
    switch (t.type) {
      case TokenType::kArrayClose:
        out->push_back(']');
        return true;
      case TokenType::kArrayOpen: {
        std::string inner;
        if (!ReadArray(lx, fmt, &inner)) return false;
        AppendJoined(out, inner);
        break;
      }
      case TokenType::kDictOpen: {
        std::string raw;
        if (!ReadDictRaw(lx, t.begin, &raw)) return false;
        AppendJoined(out, raw);
        break;
      }
      case TokenType::kNumber:
        AppendJoined(out, fmt(lx.Text(t)));
        break;
      case TokenType::kKeyword: {
        const std::string_view kw = lx.Text(t);
        if (kw == "BI" || kw == "ID" || kw == "EI") return false;
        AppendJoined(out, std::string(kw));
        break;
      }
      case TokenType::kDictClose:
      case TokenType::kBraceOpen:
      case TokenType::kBraceClose:
      case TokenType::kName:
      case TokenType::kLitString:
      case TokenType::kHexString:
        AppendJoined(out, std::string(lx.Text(t)));
        break;
    }
  }
  return false;
}

}

bool Lexer::Next(Token* tok) {
  while (pos_ < size_) {
    const uint8_t c = data_[pos_];
    if (IsWhitespace(c)) {
      ++pos_;
      continue;
    }
    if (c == '%') {
      while (pos_ < size_ && data_[pos_] != '\r' && data_[pos_] != '\n')
        ++pos_;
      continue;
    }
    break;
  }
  if (pos_ >= size_) return false;
  const size_t start = pos_;
  const uint8_t c = data_[pos_];
  switch (c) {
    case '(': {
      int depth = 0;
      while (pos_ < size_) {
        const uint8_t ch = data_[pos_];
        if (ch == '\\') {
          pos_ += 2;
          continue;
        }
        if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          if (--depth == 0) {
            ++pos_;
            *tok = {TokenType::kLitString, start, pos_};
            return true;
          }
        }
        ++pos_;
      }
      pos_ = size_;
      ok_ = false;
      return false;
    }
    case '<':
      if (pos_ + 1 < size_ && data_[pos_ + 1] == '<') {
        pos_ += 2;
        *tok = {TokenType::kDictOpen, start, pos_};
        return true;
      }
      ++pos_;
      while (pos_ < size_ && data_[pos_] != '>') ++pos_;
      if (pos_ >= size_) {
        ok_ = false;
        return false;
      }
      ++pos_;
      *tok = {TokenType::kHexString, start, pos_};
      return true;
    case '>':
      if (pos_ + 1 < size_ && data_[pos_ + 1] == '>') {
        pos_ += 2;
        *tok = {TokenType::kDictClose, start, pos_};
        return true;
      }
      ok_ = false;
      return false;
    case ')':
      ok_ = false;
      return false;
    case '[':
      ++pos_;
      *tok = {TokenType::kArrayOpen, start, pos_};
      return true;
    case ']':
      ++pos_;
      *tok = {TokenType::kArrayClose, start, pos_};
      return true;
    case '{':
      ++pos_;
      *tok = {TokenType::kBraceOpen, start, pos_};
      return true;
    case '}':
      ++pos_;
      *tok = {TokenType::kBraceClose, start, pos_};
      return true;
    case '/':
      ++pos_;
      while (pos_ < size_ && IsRegular(data_[pos_])) ++pos_;
      *tok = {TokenType::kName, start, pos_};
      return true;
    default:
      break;
  }
  while (pos_ < size_ && IsRegular(data_[pos_])) ++pos_;
  const std::string_view text(reinterpret_cast<const char*>(data_) + start,
                              pos_ - start);
  *tok = {IsNumberText(text) ? TokenType::kNumber : TokenType::kKeyword, start,
          pos_};
  return true;
}

bool IsNumberText(std::string_view tok) {
  size_t i = 0;
  if (i < tok.size() && (tok[i] == '+' || tok[i] == '-')) ++i;
  size_t int_digits = 0;
  while (i < tok.size() && IsDigit(tok[i])) {
    ++i;
    ++int_digits;
  }
  size_t frac_digits = 0;
  if (i < tok.size() && tok[i] == '.') {
    ++i;
    while (i < tok.size() && IsDigit(tok[i])) {
      ++i;
      ++frac_digits;
    }
  }
  return i == tok.size() && (int_digits + frac_digits) > 0;
}

std::string CanonicalNumber(std::string_view tok) {
  const SplitNumber s = Split(tok);
  return Assemble(s.neg, s.int_part, s.frac_part);
}

std::string RoundNumber(std::string_view tok,
                        int sig_digits,
                        int min_decimals,
                        int max_decimals) {
  const SplitNumber s = Split(tok);

  int exponent = 0;
  if (!s.int_part.empty()) {
    exponent = static_cast<int>(s.int_part.size()) - 1;
  } else {
    size_t k = 0;
    while (k < s.frac_part.size() && s.frac_part[k] == '0') ++k;
    if (k == s.frac_part.size()) return "0";
    exponent = -static_cast<int>(k + 1);
  }
  int keep = sig_digits - 1 - exponent;
  if (keep < min_decimals) keep = min_decimals;
  if (max_decimals >= 0 && keep > max_decimals) keep = max_decimals;
  if (keep < 0) keep = 0;
  if (static_cast<size_t>(keep) >= s.frac_part.size())
    return Assemble(s.neg, s.int_part, s.frac_part);
  std::string digits(s.int_part);
  digits.append(s.frac_part.substr(0, static_cast<size_t>(keep)));
  if (s.frac_part[static_cast<size_t>(keep)] >= '5') {
    int j = static_cast<int>(digits.size()) - 1;
    while (j >= 0) {
      if (digits[j] == '9') {
        digits[j] = '0';
        --j;
      } else {
        ++digits[j];
        break;
      }
    }
    if (j < 0) digits.insert(digits.begin(), '1');
  }
  const size_t int_len = digits.size() - static_cast<size_t>(keep);
  return Assemble(s.neg, std::string_view(digits).substr(0, int_len),
                  std::string_view(digits).substr(int_len));
}

double ParseNumber(std::string_view tok) {
  const SplitNumber s = Split(tok);
  double v = 0;
  for (char c : s.int_part) v = v * 10 + (c - '0');
  double scale = 0.1;
  for (char c : s.frac_part) {
    v += (c - '0') * scale;
    scale *= 0.1;
  }
  return s.neg ? -v : v;
}

std::string FormatDouble(double v) {
  if (!std::isfinite(v)) return "0";
  int decimals = 9;
  while (decimals > 0 && std::fabs(v) * std::pow(10.0, decimals) > 9.0e17)
    --decimals;
  const long long scaled =
      std::llround(v * std::pow(10.0, decimals));
  const bool neg = scaled < 0;
  std::string digits = std::to_string(neg ? -scaled : scaled);
  if (static_cast<int>(digits.size()) <= decimals)
    digits.insert(0, static_cast<size_t>(decimals) + 1 - digits.size(), '0');
  const size_t int_len = digits.size() - static_cast<size_t>(decimals);
  return Assemble(neg, std::string_view(digits).substr(0, int_len),
                  std::string_view(digits).substr(int_len));
}

bool CompactContent(const uint8_t* data,
                    size_t size,
                    const CompactOptions& opts,
                    std::string* out) {
  out->clear();
  out->reserve(size);
  const bool lossy = opts.precision > 0;
  auto fmt_coord = [&](std::string_view t) -> std::string {
    return lossy ? RoundNumber(t, opts.precision, 3, -1) : CanonicalNumber(t);
  };
  auto fmt_color = [&](std::string_view t) -> std::string {
    return lossy ? RoundNumber(t, 99, 0, 3) : CanonicalNumber(t);
  };
  auto device_ncomp = [&](const std::string& name) -> int {
    if (name == "DeviceGray") return 1;
    if (name == "DeviceRGB") return 3;
    if (name == "DeviceCMYK") return 4;
    if (opts.device_spaces) {
      auto it = opts.device_spaces->find(name);
      if (it != opts.device_spaces->end()) return it->second;
    }
    return 0;
  };
  auto device_op = [](int ncomp, bool stroke) -> const char* {
    if (ncomp == 1) return stroke ? "G" : "g";
    if (ncomp == 3) return stroke ? "RG" : "rg";
    return stroke ? "K" : "k";
  };
  auto initial_color = [](int ncomp) -> const char* {
    if (ncomp == 1) return "0";
    if (ncomp == 3) return "0 0 0";
    return "0 0 0 1";
  };

  Lexer lx(data, size);
  std::vector<Operand> operands;

  bool path_open = false;
  bool has_cp = false;
  bool clip_pending = false;
  std::string cp_x, cp_y, start_x, start_y;

  int pending_fill = 0;
  int pending_stroke = 0;

  auto flush_pending = [&]() {
    if (pending_fill) {
      out->append(initial_color(pending_fill));
      out->push_back(' ');
      out->append(device_op(pending_fill, false));
      out->push_back('\n');
      pending_fill = 0;
    }
    if (pending_stroke) {
      out->append(initial_color(pending_stroke));
      out->push_back(' ');
      out->append(device_op(pending_stroke, true));
      out->push_back('\n');
      pending_stroke = 0;
    }
  };
  auto emit = [&](const std::vector<std::string>& opnds, std::string_view op) {
    std::string line;
    for (const std::string& o : opnds) AppendJoined(&line, o);
    if (!line.empty() && !EndsTight(line)) line.push_back(' ');
    line.append(op.data(), op.size());
    line.push_back('\n');
    out->append(line);
  };
  auto all_numbers = [&](size_t n) {
    if (operands.size() != n) return false;
    for (const Operand& o : operands)
      if (o.kind != OpndKind::kNumber) return false;
    return true;
  };

  Token t;
  while (lx.Next(&t)) {
    switch (t.type) {
      case TokenType::kNumber:
        operands.push_back({OpndKind::kNumber, std::string(lx.Text(t))});
        continue;
      case TokenType::kName:
        operands.push_back({OpndKind::kName, std::string(lx.Text(t))});
        continue;
      case TokenType::kLitString:
      case TokenType::kHexString:
        operands.push_back({OpndKind::kString, std::string(lx.Text(t))});
        continue;
      case TokenType::kArrayOpen: {
        std::string arr;
        if (!ReadArray(lx, fmt_coord, &arr)) {
          out->clear();
          return false;
        }
        operands.push_back({OpndKind::kArray, std::move(arr)});
        continue;
      }
      case TokenType::kDictOpen: {
        std::string raw;
        if (!ReadDictRaw(lx, t.begin, &raw)) {
          out->clear();
          return false;
        }
        operands.push_back({OpndKind::kDict, std::move(raw)});
        continue;
      }
      case TokenType::kArrayClose:
      case TokenType::kDictClose:
      case TokenType::kBraceOpen:
      case TokenType::kBraceClose:
        operands.push_back({OpndKind::kOther, std::string(lx.Text(t))});
        continue;
      case TokenType::kKeyword:
        break;
    }
    const std::string_view op = lx.Text(t);
    if (op == "true" || op == "false" || op == "null" || IsNumericLike(op)) {
      operands.push_back({OpndKind::kOther, std::string(op)});
      continue;
    }
    if (op == "BI" || op == "ID" || op == "EI") {
      out->clear();
      return false;
    }

    const bool color = IsColorOperator(op);
    std::vector<std::string> f;
    f.reserve(operands.size());
    for (const Operand& o : operands) {
      if (o.kind == OpndKind::kNumber)
        f.push_back(color ? fmt_color(o.text) : fmt_coord(o.text));
      else
        f.push_back(o.text);
    }

    if ((op == "sc" || op == "scn") && pending_fill &&
        all_numbers(static_cast<size_t>(pending_fill))) {
      const int n = pending_fill;
      pending_fill = 0;
      flush_pending();
      emit(f, device_op(n, false));
      operands.clear();
      continue;
    }
    if ((op == "SC" || op == "SCN") && pending_stroke &&
        all_numbers(static_cast<size_t>(pending_stroke))) {
      const int n = pending_stroke;
      pending_stroke = 0;
      flush_pending();
      emit(f, device_op(n, true));
      operands.clear();
      continue;
    }
    flush_pending();
    if ((op == "cs" || op == "CS") && operands.size() == 1 &&
        operands[0].kind == OpndKind::kName) {
      const int n = device_ncomp(DecodeName(operands[0].text));
      if (n) {
        (op == "cs" ? pending_fill : pending_stroke) = n;
        operands.clear();
        continue;
      }
    }

    bool drop = false;
    bool rewritten = false;
    if (op == "m" && all_numbers(2)) {
      cp_x = start_x = f[0];
      cp_y = start_y = f[1];
      has_cp = true;
      path_open = true;
    } else if (op == "re" && all_numbers(4)) {
      cp_x = start_x = f[0];
      cp_y = start_y = f[1];
      has_cp = true;
      path_open = true;
    } else if (op == "l" && all_numbers(2)) {
      if (path_open) {
        cp_x = f[0];
        cp_y = f[1];
      }
    } else if (op == "c" && all_numbers(6)) {
      if (path_open && has_cp && f[0] == cp_x && f[1] == cp_y) {
        emit({f[2], f[3], f[4], f[5]}, "v");
        rewritten = true;
      } else if (path_open && has_cp && f[2] == f[4] && f[3] == f[5]) {
        emit({f[0], f[1], f[4], f[5]}, "y");
        rewritten = true;
      }
      if (path_open) {
        cp_x = f[4];
        cp_y = f[5];
      }
    } else if ((op == "v" || op == "y") && all_numbers(4)) {
      if (path_open) {
        cp_x = f[2];
        cp_y = f[3];
      }
    } else if (op == "h") {
      if (path_open) {
        cp_x = start_x;
        cp_y = start_y;
      }
    } else if (op == "W" || op == "W*") {
      clip_pending = true;
    } else if (IsPathPaint(op)) {
      if (op == "n" && !path_open && !clip_pending && operands.empty())
        drop = true;
      path_open = false;
      has_cp = false;
      clip_pending = false;
    } else if (op == "cm" && all_numbers(6) && f[0] == "1" && f[1] == "0" &&
               f[2] == "0" && f[3] == "1" && f[4] == "0" && f[5] == "0") {
      drop = true;
    } else if (path_open) {
      has_cp = false;
    }
    if (!drop && !rewritten) emit(f, op);
    operands.clear();
  }
  if (!lx.ok()) {
    out->clear();
    return false;
  }
  flush_pending();
  if (!operands.empty()) {
    std::vector<std::string> f;
    for (const Operand& o : operands)
      f.push_back(o.kind == OpndKind::kNumber ? fmt_coord(o.text) : o.text);
    std::string line;
    for (const std::string& o : f) AppendJoined(&line, o);
    out->append(line);
    out->push_back('\n');
  }
  return true;
}

bool PatchDoSites(const uint8_t* data,
                  size_t size,
                  const std::vector<DoPatch>& patches,
                  PatchResult* result) {
  result->content.clear();
  result->uses.clear();
  result->applied = 0;
  struct Splice {
    size_t begin;
    size_t end;
    std::string text;
  };
  std::vector<Splice> splices;
  std::map<std::string, int, std::less<>> occurrence;
  std::vector<bool> used(patches.size(), false);

  Lexer lx(data, size);
  Token t;
  size_t operand_begin = SIZE_MAX;
  int operand_count = 0;
  std::string last_name;
  bool last_is_name = false;
  int text_depth = 0;
  while (lx.Next(&t)) {
    if (t.type != TokenType::kKeyword) {
      if (operand_begin == SIZE_MAX) operand_begin = t.begin;
      ++operand_count;
      last_is_name = t.type == TokenType::kName;
      if (last_is_name) last_name = DecodeName(lx.Text(t));
      if (t.type == TokenType::kArrayOpen) {

        std::string skip;
        if (!ReadArray(lx, [](std::string_view s) { return std::string(s); },
                       &skip))
          return false;
      } else if (t.type == TokenType::kDictOpen) {
        std::string skip;
        if (!ReadDictRaw(lx, t.begin, &skip)) return false;
      }
      continue;
    }
    const std::string_view op = lx.Text(t);
    if (op == "true" || op == "false" || op == "null" || IsNumericLike(op)) {
      if (operand_begin == SIZE_MAX) operand_begin = t.begin;
      ++operand_count;
      last_is_name = false;
      continue;
    }
    if (op == "BI" || op == "ID" || op == "EI") return false;
    if (op == "BT") ++text_depth;
    if (op == "ET" && text_depth > 0) --text_depth;
    if (op == "Do" && operand_count >= 1 && last_is_name) {
      const int occ = occurrence[last_name]++;
      bool patched = false;
      for (size_t i = 0; i < patches.size(); ++i) {
        const DoPatch& p = patches[i];
        if (used[i] || p.occurrence != occ || p.name != last_name) continue;
        if (p.has_matrix && text_depth > 0) return false;
        std::string rep;
        if (p.has_matrix) {
          rep = "q ";
          for (int k = 0; k < 6; ++k) {
            rep += FormatDouble(p.m[k]);
            rep.push_back(' ');
          }
          rep += "cm ";
        }
        rep.push_back('/');
        rep += p.new_name;
        rep += " Do";
        if (p.has_matrix) rep += " Q";
        splices.push_back({operand_begin, t.end, std::move(rep)});
        used[i] = true;
        patched = true;
        result->uses[p.new_name]++;
        break;
      }
      if (!patched) result->uses[last_name]++;
    }
    operand_begin = SIZE_MAX;
    operand_count = 0;
    last_is_name = false;
  }
  if (!lx.ok()) return false;
  for (bool u : used)
    if (!u) return false;
  size_t at = 0;
  result->content.reserve(size + splices.size() * 48);
  for (const Splice& s : splices) {
    result->content.append(reinterpret_cast<const char*>(data) + at,
                           s.begin - at);
    result->content.append(s.text);
    at = s.end;
  }
  result->content.append(reinterpret_cast<const char*>(data) + at, size - at);
  result->applied = static_cast<int>(splices.size());
  return true;
}

bool WalkDoOperators(const uint8_t* data,
                     size_t size,
                     const std::function<void(const DoEvent&)>& on_do,
                     bool* has_inline_image) {
  *has_inline_image = false;
  Lexer lx(data, size);
  Token t;
  std::vector<std::string_view> nums;
  int operand_count = 0;
  std::string_view last_name;
  bool last_is_name = false;
  int text_depth = 0;
  double ctm[6] = {1, 0, 0, 1, 0, 0};
  std::vector<std::array<double, 6>> stack;
  std::map<std::string, int, std::less<>> occurrence;
  while (lx.Next(&t)) {
    if (t.type != TokenType::kKeyword) {
      ++operand_count;
      last_is_name = t.type == TokenType::kName;
      if (last_is_name) last_name = lx.Text(t);
      if (t.type == TokenType::kNumber) nums.push_back(lx.Text(t));
      if (t.type == TokenType::kArrayOpen) {
        std::string skip;
        if (!ReadArray(lx, [](std::string_view s) { return std::string(s); },
                       &skip))
          return false;
      } else if (t.type == TokenType::kDictOpen) {
        std::string skip;
        if (!ReadDictRaw(lx, t.begin, &skip)) return false;
      }
      continue;
    }
    const std::string_view op = lx.Text(t);
    if (op == "true" || op == "false" || op == "null" || IsNumericLike(op)) {
      ++operand_count;
      last_is_name = false;
      continue;
    }
    if (op == "BI") {
      *has_inline_image = true;
      return true;
    }
    if (op == "q") {
      std::array<double, 6> saved;
      std::memcpy(saved.data(), ctm, sizeof(ctm));
      stack.push_back(saved);
    } else if (op == "Q") {
      if (!stack.empty()) {
        std::memcpy(ctm, stack.back().data(), sizeof(ctm));
        stack.pop_back();
      }
    } else if (op == "cm" && operand_count == 6 && nums.size() == 6) {
      double m[6];
      for (int i = 0; i < 6; ++i) m[i] = ParseNumber(nums[i]);
      Concat(m, ctm, ctm);
    } else if (op == "BT") {
      ++text_depth;
    } else if (op == "ET") {
      if (text_depth > 0) --text_depth;
    } else if (op == "Do" && operand_count >= 1 && last_is_name) {
      DoEvent ev;
      const std::string name = DecodeName(last_name);
      ev.name = name;
      ev.occurrence = occurrence[name]++;
      std::memcpy(ev.ctm, ctm, sizeof(ctm));
      ev.in_text = text_depth > 0;
      on_do(ev);
    }
    nums.clear();
    operand_count = 0;
    last_is_name = false;
  }
  return lx.ok();
}

bool Deflate(const uint8_t* in, size_t size, int level,
             std::vector<uint8_t>* out) {
  z_stream zs;
  std::memset(&zs, 0, sizeof(zs));
  if (deflateInit(&zs, level) != Z_OK) return false;
  out->resize(deflateBound(&zs, static_cast<uLong>(size)));
  zs.next_in = const_cast<Bytef*>(in);
  zs.avail_in = static_cast<uInt>(size);
  zs.next_out = out->data();
  zs.avail_out = static_cast<uInt>(out->size());
  const int rc = deflate(&zs, Z_FINISH);
  const size_t produced = zs.total_out;
  deflateEnd(&zs);
  if (rc != Z_STREAM_END) {
    out->clear();
    return false;
  }
  out->resize(produced);
  return true;
}

bool Reflate(const uint8_t* in, size_t size, int level,
             std::vector<uint8_t>* out) {
  out->clear();
  z_stream inf;
  std::memset(&inf, 0, sizeof(inf));
  if (inflateInit(&inf) != Z_OK) return false;
  z_stream def;
  std::memset(&def, 0, sizeof(def));
  if (deflateInit(&def, level) != Z_OK) {
    inflateEnd(&inf);
    return false;
  }
  std::vector<uint8_t> chunk(256 * 1024);
  std::vector<uint8_t> dchunk(256 * 1024);
  inf.next_in = const_cast<Bytef*>(in);
  inf.avail_in = static_cast<uInt>(size);
  bool ok = false;
  bool inflate_done = false;
  for (;;) {
    inf.next_out = chunk.data();
    inf.avail_out = static_cast<uInt>(chunk.size());
    const int irc = inflate(&inf, Z_NO_FLUSH);
    if (irc != Z_OK && irc != Z_STREAM_END) break;
    const size_t got = chunk.size() - inf.avail_out;
    if (irc == Z_STREAM_END) inflate_done = true;
    def.next_in = chunk.data();
    def.avail_in = static_cast<uInt>(got);
    const int flush = inflate_done ? Z_FINISH : Z_NO_FLUSH;
    int drc = Z_OK;
    do {
      def.next_out = dchunk.data();
      def.avail_out = static_cast<uInt>(dchunk.size());
      drc = deflate(&def, flush);
      if (drc == Z_STREAM_ERROR) break;
      out->insert(out->end(), dchunk.data(),
                  dchunk.data() + (dchunk.size() - def.avail_out));
    } while (def.avail_out == 0 || (flush == Z_FINISH && drc != Z_STREAM_END));
    if (drc == Z_STREAM_ERROR) break;
    if (inflate_done) {
      ok = drc == Z_STREAM_END;
      break;
    }
    if (got == 0 && inf.avail_in == 0) break;
  }
  inflateEnd(&inf);
  deflateEnd(&def);
  if (!ok) out->clear();
  return ok;
}

bool DeflateParallel(const uint8_t* in, size_t size, int level, int threads,
                     std::vector<uint8_t>* out) {
  constexpr size_t kBlock = 256u * 1024;
  constexpr size_t kDict = 32768;

  if (size < 2 * kBlock) return Deflate(in, size, level, out);
  const size_t nblocks = (size + kBlock - 1) / kBlock;
  std::vector<std::vector<uint8_t>> parts(nblocks);
  std::atomic<bool> ok{true};
  ParallelFor(nblocks, threads, [&](size_t i) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
      ok = false;
      return;
    }
    const size_t off = i * kBlock;
    const size_t len = std::min(kBlock, size - off);
    if (i > 0) {
      const size_t dlen = std::min(kDict, off);
      deflateSetDictionary(&zs, in + off - dlen, static_cast<uInt>(dlen));
    }
    std::vector<uint8_t>& p = parts[i];
    p.resize(deflateBound(&zs, static_cast<uLong>(len)) + 64);
    zs.next_in = const_cast<Bytef*>(in + off);
    zs.avail_in = static_cast<uInt>(len);
    zs.next_out = p.data();
    zs.avail_out = static_cast<uInt>(p.size());
    const bool last = i + 1 == nblocks;
    const int rc = deflate(&zs, last ? Z_FINISH : Z_SYNC_FLUSH);
    if ((last && rc != Z_STREAM_END) || (!last && rc != Z_OK) ||
        zs.avail_in != 0)
      ok = false;
    p.resize(zs.total_out);
    deflateEnd(&zs);
  });
  if (!ok) return Deflate(in, size, level, out);

  const unsigned flevel = level >= 7 ? 3 : level >= 6 ? 2 : level >= 2 ? 1 : 0;
  const unsigned cmf = 0x78;
  unsigned flg = flevel << 6;
  flg += 31 - ((cmf << 8) | flg) % 31;
  size_t total = 6;
  for (const auto& p : parts) total += p.size();
  out->clear();
  out->reserve(total);
  out->push_back(static_cast<uint8_t>(cmf));
  out->push_back(static_cast<uint8_t>(flg));
  for (const auto& p : parts) out->insert(out->end(), p.begin(), p.end());
  uLong adler = adler32(0L, Z_NULL, 0);
  for (size_t off = 0; off < size; off += kBlock) {
    const size_t len = std::min(kBlock, size - off);
    adler = adler32(adler, in + off, static_cast<uInt>(len));
  }
  out->push_back(static_cast<uint8_t>(adler >> 24));
  out->push_back(static_cast<uint8_t>(adler >> 16));
  out->push_back(static_cast<uint8_t>(adler >> 8));
  out->push_back(static_cast<uint8_t>(adler));
  return true;
}

bool Inflate(const uint8_t* in, size_t size, size_t max_out,
             std::vector<uint8_t>* out) {
  out->clear();
  z_stream zs;
  std::memset(&zs, 0, sizeof(zs));
  if (inflateInit(&zs) != Z_OK) return false;
  zs.next_in = const_cast<Bytef*>(in);
  zs.avail_in = static_cast<uInt>(size);
  std::vector<uint8_t> chunk(256 * 1024);
  bool ok = false;
  for (;;) {
    zs.next_out = chunk.data();
    zs.avail_out = static_cast<uInt>(chunk.size());
    const int rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) break;
    const size_t got = chunk.size() - zs.avail_out;
    if (out->size() + got > max_out) break;
    out->insert(out->end(), chunk.data(), chunk.data() + got);
    if (rc == Z_STREAM_END) {
      ok = true;
      break;
    }
    if (got == 0 && zs.avail_in == 0) break;
  }
  inflateEnd(&zs);
  if (!ok) out->clear();
  return ok;
}

int ZlibHeaderLevel(const uint8_t* in, size_t size) {
  if (size < 2) return -1;
  const unsigned cmf = in[0];
  const unsigned flg = in[1];
  if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0) return -1;
  return static_cast<int>(flg >> 6);
}

int ResolveThreadCount(int requested) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  return 1;
#endif
  if (requested > 0) return std::min(requested, 64);
  const unsigned hw = std::thread::hardware_concurrency();
  return static_cast<int>(std::clamp(hw, 1u, 8u));
}

void ParallelFor(size_t n, int threads, const std::function<void(size_t)>& fn) {
  if (n == 0) return;
  const size_t workers =
      std::min(static_cast<size_t>(std::max(threads, 1)), n);
  if (workers <= 1) {
    for (size_t i = 0; i < n; ++i) fn(i);
    return;
  }
  std::atomic<size_t> next{0};
  auto run = [&]() {
    for (;;) {
      const size_t i = next.fetch_add(1);
      if (i >= n) return;
      fn(i);
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(workers - 1);
  for (size_t w = 1; w < workers; ++w) pool.emplace_back(run);
  run();
  for (std::thread& th : pool) th.join();
}

}
