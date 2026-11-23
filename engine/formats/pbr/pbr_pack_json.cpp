#include <cctype>
#include <cstdio>

#include "pbr_pack.h"

namespace pbr {

Json& Json::set(const std::string& key, Json v) {
  if (kind != Obj) {
    kind = Obj;
    o.clear();
  }
  for (auto& kv : o) {
    if (kv.first == key) {
      kv.second = std::move(v);
      return kv.second;
    }
  }
  o.emplace_back(key, std::move(v));
  return o.back().second;
}

Json* Json::find(const std::string& key) {
  for (auto& kv : o)
    if (kv.first == key) return &kv.second;
  return nullptr;
}
const Json* Json::find(const std::string& key) const {
  for (auto& kv : o)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

namespace {

void put_string(std::string& out, const std::string& s, bool ascii) {
  out += '"';
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    size_t n = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 6 && i + 1 < s.size()) {
      cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
      n = 2;
    } else if ((c >> 4) == 14 && i + 2 < s.size()) {
      cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
      n = 3;
    } else if ((c >> 3) == 30 && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
      n = 4;
    } else {
      cp = c;
    }
    const size_t start = i;
    i += n;
    char buf[16];
    switch (cp) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (cp < 0x20 || (ascii && cp > 0x7E)) {
          if (cp >= 0x10000) {
            cp -= 0x10000;
            std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
            out += buf;
          } else {
            std::snprintf(buf, sizeof buf, "\\u%04x", cp);
            out += buf;
          }
        } else if (cp < 0x80) {
          out += (char)cp;
        } else {
          out.append(s, start, n);
        }
    }
  }
  out += '"';
}

void dump_to(const Json& j, std::string& out, int indent, int level, bool ascii) {
  auto newline = [&](int lv) {
    if (indent >= 0) {
      out += '\n';
      out.append((size_t)(indent * lv), ' ');
    }
  };
  switch (j.kind) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Int: out += std::to_string(j.i); break;
    case Json::Num: out += j.s; break;
    case Json::Str: put_string(out, j.s, ascii); break;
    case Json::Arr:
      if (j.a.empty()) {
        out += "[]";
        break;
      }
      out += '[';
      for (size_t k = 0; k < j.a.size(); k++) {
        if (k) out += indent >= 0 ? "," : ", ";
        newline(level + 1);
        dump_to(j.a[k], out, indent, level + 1, ascii);
      }
      newline(level);
      out += ']';
      break;
    case Json::Obj:
      if (j.o.empty()) {
        out += "{}";
        break;
      }
      out += '{';
      for (size_t k = 0; k < j.o.size(); k++) {
        if (k) out += indent >= 0 ? "," : ", ";
        newline(level + 1);
        put_string(out, j.o[k].first, ascii);
        out += ": ";
        dump_to(j.o[k].second, out, indent, level + 1, ascii);
      }
      newline(level);
      out += '}';
      break;
  }
}

}

std::string Json::dump(int indent, bool ensure_ascii) const {
  std::string out;
  dump_to(*this, out, indent, 0, ensure_ascii);
  return out;
}

namespace {

struct Parser {
  const std::string& t;
  size_t p = 0;
  explicit Parser(const std::string& s) : t(s) {}
  [[noreturn]] void bad(const char* w) const { throw Fail(std::string("JSON: ") + w + " at " + std::to_string(p)); }
  void ws() {
    while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) p++;
  }
  static void utf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  }
  uint32_t hex4() {
    if (p + 4 > t.size()) bad("short escape");
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      char c = t[p++];
      v = v * 16 + (uint32_t)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : (bad("bad hex"), 0));
    }
    return v;
  }
  std::string str() {
    if (t[p] != '"') bad("string expected");
    p++;
    std::string o;
    while (true) {
      if (p >= t.size()) bad("unterminated string");
      char c = t[p++];
      if (c == '"') break;
      if (c != '\\') { o += c; continue; }
      if (p >= t.size()) bad("bad escape");
      char e = t[p++];
      switch (e) {
        case 'n': o += '\n'; break;
        case 'r': o += '\r'; break;
        case 't': o += '\t'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case '/': o += '/'; break;
        case '\\': o += '\\'; break;
        case '"': o += '"'; break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
            size_t save = p;
            p += 2;
            uint32_t lo = hex4();
            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            else p = save;
          }
          utf8(o, cp);
          break;
        }
        default: bad("bad escape");
      }
    }
    return o;
  }
  Json value() {
    ws();
    if (p >= t.size()) bad("unexpected end");
    char c = t[p];
    if (c == '{') {
      p++;
      Json j = Json::object();
      ws();
      if (t[p] == '}') { p++; return j; }
      while (true) {
        ws();
        std::string k = str();
        ws();
        if (t[p] != ':') bad("':' expected");
        p++;
        j.set(k, value());
        ws();
        if (t[p] == ',') { p++; continue; }
        if (t[p] == '}') { p++; break; }
        bad("',' or '}' expected");
      }
      return j;
    }
    if (c == '[') {
      p++;
      Json j = Json::array();
      ws();
      if (t[p] == ']') { p++; return j; }
      while (true) {
        j.push(value());
        ws();
        if (t[p] == ',') { p++; continue; }
        if (t[p] == ']') { p++; break; }
        bad("',' or ']' expected");
      }
      return j;
    }
    if (c == '"') return Json(str());
    if (t.compare(p, 4, "true") == 0) { p += 4; return Json(true); }
    if (t.compare(p, 5, "false") == 0) { p += 5; return Json(false); }
    if (t.compare(p, 4, "null") == 0) { p += 4; return Json(); }
    size_t s = p;
    bool isint = true;
    if (t[p] == '-') p++;
    while (p < t.size() && (isdigit((unsigned char)t[p]) || t[p] == '.' || t[p] == 'e' || t[p] == 'E' || t[p] == '+' || t[p] == '-')) {
      if (!isdigit((unsigned char)t[p])) isint = false;
      p++;
    }
    if (p == s) bad("value expected");
    if (isint && p - s < 18) return Json((int64_t)std::stoll(t.substr(s, p - s)));
    return Json::num(t.substr(s, p - s));
  }
};

}

Json Json::parse(const std::string& text) {
  Parser ps(text);
  Json j = ps.value();
  ps.ws();
  if (ps.p != text.size()) ps.bad("trailing text");
  return j;
}

}
