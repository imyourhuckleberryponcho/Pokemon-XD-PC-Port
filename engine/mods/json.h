#ifndef ENGINE_MODS_JSON_H
#define ENGINE_MODS_JSON_H

#include <charconv>
#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
#include <port_mac_from_chars.h>
#endif

namespace engine::mods::json {

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Value> items;
  std::map<std::string, Value> fields;
  std::vector<std::string> order;
  size_t line = 0;
};

inline std::string Number(double n) {
  if (n == static_cast<double>(static_cast<long long>(n)) && n > -1e15 && n < 1e15) {
    return std::to_string(static_cast<long long>(n));
  }
  char text[32];
  std::snprintf(text, sizeof(text), "%g", n);
  return text;
}

class Reader {
public:
  explicit Reader(std::string_view text) : mText(text) {}

  bool Document(Value& out) {
    if (mText.substr(0, 3) == "\xEF\xBB\xBF") {
      mPos = 3;
    }
    if (!Parse(out, 0)) {
      return false;
    }
    Space();
    return mPos == mText.size() || Fail("unexpected text after the end");
  }
  std::string Error() const { return mError; }

private:
  std::string_view mText;
  size_t mPos = 0;
  std::string mError;

  size_t mLine = 1, mLineAt = 0;
  size_t Line() {
    for (; mLineAt < mPos && mLineAt < mText.size(); mLineAt++) {
      mLine += mText[mLineAt] == '\n';
    }
    return mLine;
  }
  bool Fail(const std::string& what) {
    if (mError.empty()) {
      mError = "line " + std::to_string(Line()) + ": " + what;
    }
    return false;
  }
  void Space() {
    while (mPos < mText.size() && (mText[mPos] == ' ' || mText[mPos] == '\t' || mText[mPos] == '\n' || mText[mPos] == '\r')) {
      mPos++;
    }
  }
  bool Word(std::string_view w) {
    if (mText.substr(mPos, w.size()) == w) {
      mPos += w.size();
      return true;
    }
    return false;
  }

  bool Parse(Value& out, int depth) {
    if (depth > 32) {
      return Fail("too deeply nested");
    }
    Space();
    if (mPos >= mText.size()) {
      return Fail("the text ends where a value should be");
    }
    out.line = Line();
    const char c = mText[mPos];
    if (c == '{') {
      mPos++;
      out.kind = Value::Kind::Object;
      Space();
      if (Word("}")) {
        return true;
      }
      for (;;) {
        Space();
        Value key;
        if (mPos >= mText.size() || mText[mPos] != '"') {
          return Fail("expected a name in quotes");
        }
        if (!String(key.text)) {
          return false;
        }
        Space();
        if (!Word(":")) {
          return Fail("expected ':' after \"" + key.text + "\"");
        }
        Value v;
        if (!Parse(v, depth + 1)) {
          return false;
        }
        if (out.fields.find(key.text) == out.fields.end()) {
          out.order.push_back(key.text);
        }
        out.fields[key.text] = std::move(v);
        Space();
        if (Word("}")) {
          return true;
        }
        if (!Word(",")) {
          return Fail("expected ',' or '}' (a ',' missing at the end of the line before?)");
        }
      }
    }
    if (c == '[') {
      mPos++;
      out.kind = Value::Kind::Array;
      Space();
      if (Word("]")) {
        return true;
      }
      for (;;) {
        Value v;
        if (!Parse(v, depth + 1)) {
          return false;
        }
        out.items.push_back(std::move(v));
        Space();
        if (Word("]")) {
          return true;
        }
        if (!Word(",")) {
          return Fail("expected ',' or ']'");
        }
      }
    }
    if (c == '"') {
      out.kind = Value::Kind::String;
      return String(out.text);
    }
    if (Word("true")) {
      out.kind = Value::Kind::Bool;
      out.boolean = true;
      return true;
    }
    if (Word("false")) {
      out.kind = Value::Kind::Bool;
      return true;
    }
    if (Word("null")) {
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t end = mPos;
      while (end < mText.size() && std::string_view("+-.eE0123456789").find(mText[end]) != std::string_view::npos) {
        end++;
      }
#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
      const auto r = port_mac::from_chars(mText.data() + mPos, mText.data() + end, out.number);
#else
      const auto r = std::from_chars(mText.data() + mPos, mText.data() + end, out.number);
#endif
      if (r.ec != std::errc() || r.ptr != mText.data() + end) {
        return Fail("not a number");
      }
      out.kind = Value::Kind::Number;
      mPos = end;
      return true;
    }
    return Fail(std::string("unexpected '") + c + "'");
  }

  static void Utf8(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | cp >> 6));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | cp >> 12));
      out.push_back(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | cp >> 18));
      out.push_back(static_cast<char>(0x80 | (cp >> 12 & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  bool Hex4(uint32_t& out) {
    out = 0;
    for (int i = 0; i < 4; i++, mPos++) {
      if (mPos >= mText.size()) {
        return Fail("unfinished \\u");
      }
      const char h = mText[mPos];
      const int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
      if (d < 0) {
        return Fail("bad \\u");
      }
      out = out << 4 | static_cast<uint32_t>(d);
    }
    return true;
  }
  bool String(std::string& out) {
    mPos++;
    while (mPos < mText.size()) {
      const char c = mText[mPos++];
      if (c == '"') {
        return true;
      }
      if (c == '\n') {
        return Fail("a text in quotes doesn't end on its line");
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (mPos >= mText.size()) {
        break;
      }
      const char e = mText[mPos++];
      switch (e) {
      case '"': case '\\': case '/': out.push_back(e); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        uint32_t cp = 0;
        if (!Hex4(cp)) {
          return false;
        }
        if (cp >= 0xD800 && cp < 0xDC00 && Word("\\u")) {
          uint32_t lo = 0;
          if (!Hex4(lo)) {
            return false;
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        Utf8(cp, out);
        break;
      }
      default: return Fail(std::string("unknown escape \\") + e);
      }
    }
    return Fail("a text in quotes doesn't end");
  }
};

}

#endif
