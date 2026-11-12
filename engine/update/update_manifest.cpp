#include "update.h"

extern "C" {
#include <monocypher.h>
#include <optional/monocypher-ed25519.h>
}

#include <cstring>
#include <memory>

namespace engine::update {

bool ParseVersion(std::string_view text, std::vector<uint32_t>& parts) {
  parts.clear();
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
    text.remove_prefix(1);
  }
  if (text.empty() || text.size() > 40) {
    return false;
  }
  uint64_t value = 0;
  bool digit = false;
  for (size_t i = 0; i <= text.size(); i++) {
    if (i == text.size() || text[i] == '.') {
      if (!digit || parts.size() == 4) {
        return false;
      }
      parts.push_back(static_cast<uint32_t>(value));
      value = 0;
      digit = false;
    } else if (text[i] >= '0' && text[i] <= '9') {
      value = value * 10 + static_cast<uint64_t>(text[i] - '0');
      if (value > 0xFFFFFFFFull) {
        return false;
      }
      digit = true;
    } else {
      return false;
    }
  }
  return !parts.empty();
}

int CompareVersions(std::string_view a, std::string_view b) {
  std::vector<uint32_t> x, y;
  if (!ParseVersion(a, x) || !ParseVersion(b, y)) {
    return 0;
  }
  for (size_t i = 0; i < 4; i++) {
    const uint32_t l = i < x.size() ? x[i] : 0, r = i < y.size() ? y[i] : 0;
    if (l != r) {
      return l < r ? -1 : 1;
    }
  }
  return 0;
}

bool IsNewer(std::string_view candidate, std::string_view current) {
  std::vector<uint32_t> x, y;
  return ParseVersion(candidate, x) && ParseVersion(current, y) && CompareVersions(candidate, current) > 0;
}

bool HexDecode(std::string_view hex, std::vector<uint8_t>& out) {
  out.clear();
  if (hex.size() % 2 != 0) {
    return false;
  }
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int h = nibble(hex[i]), l = nibble(hex[i + 1]);
    if (h < 0 || l < 0) {
      out.clear();
      return false;
    }
    out.push_back(static_cast<uint8_t>(h * 16 + l));
  }
  return true;
}

std::string HexEncode(const uint8_t* data, size_t size) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; i++) {
    out += kDigits[data[i] >> 4];
    out += kDigits[data[i] & 15];
  }
  return out;
}

bool VerifyEd25519(const uint8_t publicKey[32], const uint8_t signature[64], std::string_view message) {
  return crypto_ed25519_check(signature, publicKey, reinterpret_cast<const uint8_t*>(message.data()),
                              message.size()) == 0;
}

void SignEd25519(const uint8_t seed[32], std::string_view message, uint8_t signature[64], uint8_t publicKey[32]) {
  uint8_t copy[32], secret[64];
  std::memcpy(copy, seed, 32);
  crypto_ed25519_key_pair(secret, publicKey, copy);
  crypto_ed25519_sign(signature, secret, reinterpret_cast<const uint8_t*>(message.data()), message.size());
  crypto_wipe(secret, sizeof secret);
}

namespace {

struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool boolean = false;
  bool integer = false;
  int64_t number = 0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;
  size_t begin = 0, end = 0;

  const Json* Member(std::string_view name) const {
    for (const auto& [key, value] : members) {
      if (key == name) {
        return &value;
      }
    }
    return nullptr;
  }
};

class Reader {
 public:
  explicit Reader(std::string_view text) : t_(text) {}

  bool Document(Json& out, std::string& error) {
    Space();
    if (!Value(out, 0)) {
      error = error_.empty() ? "not JSON" : error_;
      return false;
    }
    Space();
    if (p_ != t_.size()) {
      error = "text after the end of the JSON";
      return false;
    }
    return true;
  }

 private:
  std::string_view t_;
  size_t p_ = 0;
  std::string error_;

  bool Fail(const char* what) {
    if (error_.empty()) {
      error_ = std::string(what) + " at byte " + std::to_string(p_);
    }
    return false;
  }
  void Space() {
    while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) {
      p_++;
    }
  }
  bool Literal(std::string_view word) {
    if (t_.substr(p_, word.size()) != word) {
      return Fail("unexpected text");
    }
    p_ += word.size();
    return true;
  }

  bool Value(Json& v, int depth) {
    if (depth > 16) {
      return Fail("nested too deeply");
    }
    if (p_ >= t_.size()) {
      return Fail("unexpected end");
    }
    v.begin = p_;
    const char c = t_[p_];
    bool ok;
    if (c == '{') {
      ok = Object(v, depth);
    } else if (c == '[') {
      ok = Array(v, depth);
    } else if (c == '"') {
      v.kind = Json::Kind::String;
      ok = String(v.text);
    } else if (c == 't') {
      v.kind = Json::Kind::Bool;
      v.boolean = true;
      ok = Literal("true");
    } else if (c == 'f') {
      v.kind = Json::Kind::Bool;
      ok = Literal("false");
    } else if (c == 'n') {
      ok = Literal("null");
    } else {
      ok = Number(v);
    }
    v.end = p_;
    return ok;
  }

  bool Object(Json& v, int depth) {
    v.kind = Json::Kind::Object;
    p_++;
    Space();
    if (p_ < t_.size() && t_[p_] == '}') {
      p_++;
      return true;
    }
    for (;;) {
      Space();
      std::string key;
      if (p_ >= t_.size() || t_[p_] != '"' || !String(key)) {
        return Fail("expected a name");
      }
      if (v.Member(key) != nullptr) {
        return Fail("a name twice");
      }
      Space();
      if (p_ >= t_.size() || t_[p_] != ':') {
        return Fail("expected ':'");
      }
      p_++;
      Space();
      Json member;
      if (!Value(member, depth + 1)) {
        return false;
      }
      v.members.emplace_back(std::move(key), std::move(member));
      Space();
      if (p_ < t_.size() && t_[p_] == ',') {
        p_++;
        continue;
      }
      if (p_ < t_.size() && t_[p_] == '}') {
        p_++;
        return true;
      }
      return Fail("expected ',' or '}'");
    }
  }

  bool Array(Json& v, int depth) {
    v.kind = Json::Kind::Array;
    p_++;
    Space();
    if (p_ < t_.size() && t_[p_] == ']') {
      p_++;
      return true;
    }
    for (;;) {
      Space();
      Json item;
      if (!Value(item, depth + 1)) {
        return false;
      }
      v.items.push_back(std::move(item));
      Space();
      if (p_ < t_.size() && t_[p_] == ',') {
        p_++;
        continue;
      }
      if (p_ < t_.size() && t_[p_] == ']') {
        p_++;
        return true;
      }
      return Fail("expected ',' or ']'");
    }
  }

  static void Utf8(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool Hex4(uint32_t& out) {
    out = 0;
    for (int i = 0; i < 4; i++, p_++) {
      if (p_ >= t_.size()) {
        return Fail("unexpected end");
      }
      const char c = t_[p_];
      const int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (n < 0) {
        return Fail("bad \\u escape");
      }
      out = out * 16 + static_cast<uint32_t>(n);
    }
    return true;
  }

  bool String(std::string& out) {
    p_++;
    while (p_ < t_.size()) {
      const char c = t_[p_++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        return Fail("control character in a string");
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (p_ >= t_.size()) {
        break;
      }
      const char e = t_[p_++];
      switch (e) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        uint32_t cp;
        if (!Hex4(cp)) {
          return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          uint32_t low;
          if (t_.substr(p_, 2) != "\\u") {
            return Fail("lone surrogate");
          }
          p_ += 2;
          if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF) {
            return Fail("bad surrogate pair");
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return Fail("lone surrogate");
        }
        Utf8(cp, out);
        break;
      }
      default: return Fail("bad escape");
      }
    }
    return Fail("unterminated string");
  }

  bool Number(Json& v) {
    v.kind = Json::Kind::Number;
    const size_t start = p_;
    if (p_ < t_.size() && t_[p_] == '-') {
      p_++;
    }
    bool digits = false, whole = true;
    while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') {
      p_++;
      digits = true;
    }
    if (p_ < t_.size() && t_[p_] == '.') {
      whole = false;
      p_++;
      while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') {
        p_++;
      }
    }
    if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
      whole = false;
      p_++;
      if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) {
        p_++;
      }
      while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') {
        p_++;
      }
    }
    if (!digits) {
      return Fail("unexpected text");
    }
    if (whole) {
      const std::string_view s = t_.substr(start, p_ - start);
      const bool negative = s.front() == '-';
      uint64_t value = 0;
      bool fits = s.size() - (negative ? 1 : 0) <= 18;
      for (char c : s.substr(negative ? 1 : 0)) {
        value = value * 10 + static_cast<uint64_t>(c - '0');
      }
      if (fits) {
        v.integer = true;
        v.number = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
      }
    }
    return true;
  }
};

bool IsLowerHex(std::string_view s, size_t length) {
  if (s.size() != length) {
    return false;
  }
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

}

bool ParseSignedManifest(std::string_view fileText, std::string_view publicKeyHex, std::string_view product,
                         Manifest& out, std::string& error) {
  out = Manifest();
  if (fileText.size() > kMaxManifestBytes) {
    error = "the update information is too large";
    return false;
  }
  if (fileText.size() >= 3 && static_cast<unsigned char>(fileText[0]) == 0xEF &&
      static_cast<unsigned char>(fileText[1]) == 0xBB && static_cast<unsigned char>(fileText[2]) == 0xBF) {
    error = "the update information starts with a byte order mark (sign_update.py never writes one)";
    return false;
  }
  std::vector<uint8_t> key;
  if (!HexDecode(publicKeyHex, key) || key.size() != 32) {
    error = "this version has no valid key to check updates with";
    return false;
  }
  Json doc;
  std::string why;
  if (!Reader(fileText).Document(doc, why)) {
    error = "the update information can't be read (" + why + ")";
    return false;
  }
  const Json* signature = doc.Member("signature");
  const Json* manifest = doc.Member("manifest");
  if (doc.kind != Json::Kind::Object || signature == nullptr || signature->kind != Json::Kind::String ||
      manifest == nullptr || manifest->kind != Json::Kind::Object) {
    error = "the update information has no signed manifest";
    return false;
  }
  std::vector<uint8_t> sig;
  if (!HexDecode(signature->text, sig) || sig.size() != 64) {
    error = "the update information's signature isn't readable";
    return false;
  }
  const std::string_view signedBytes = fileText.substr(manifest->begin, manifest->end - manifest->begin);
  std::string message(kSignedPrefix);
  message.append(signedBytes);
  if (!VerifyEd25519(key.data(), sig.data(), message)) {
    error = "the update information's signature is wrong: it was not made by this program's publisher, or it was "
            "changed after signing";
    return false;
  }
  const auto text = [&](const char* name, std::string& into, bool required) {
    const Json* v = manifest->Member(name);
    if (v == nullptr) {
      return !required;
    }
    if (v->kind != Json::Kind::String) {
      return false;
    }
    into = v->text;
    return !required || !into.empty();
  };
  const Json* format = manifest->Member("format");
  if (format == nullptr || format->kind != Json::Kind::Number || !format->integer || format->number != 1) {
    error = "the update information is in a newer format: this version can't read it (download the new version "
            "by hand)";
    return false;
  }
  Manifest m;
  if (!text("product", m.product, true) || !text("version", m.version, true) || !text("url", m.url, true) ||
      !text("sha256", m.sha256, true) || !text("date", m.date, false) || !text("notes", m.notes, false)) {
    error = "the update information is missing something (product, version, url, sha256)";
    return false;
  }
  if (m.product != product) {
    error = "the update information is for another program (" + m.product + ")";
    return false;
  }
  std::vector<uint32_t> parts;
  if (!ParseVersion(m.version, parts)) {
    error = "the update information's version isn't readable: " + m.version;
    return false;
  }
  if (!IsLowerHex(m.sha256, 64)) {
    error = "the update information's SHA-256 isn't readable";
    return false;
  }
  const Json* size = manifest->Member("size");
  if (size == nullptr || size->kind != Json::Kind::Number || !size->integer || size->number <= 0 ||
      static_cast<uint64_t>(size->number) > kMaxPackageBytes) {
    error = "the update information's package size isn't usable";
    return false;
  }
  m.size = static_cast<uint64_t>(size->number);
  const std::string_view url = m.url;
  if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0 && url.rfind("file:///", 0) != 0) {
    error = "the update information's download address isn't usable";
    return false;
  }
  out = std::move(m);
  return true;
}

}
