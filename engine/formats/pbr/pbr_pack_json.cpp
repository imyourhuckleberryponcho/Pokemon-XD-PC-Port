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

void put_string(std::string& out, const std::string& s) {
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
        if (cp < 0x20 || cp > 0x7E) {
          if (cp == 0x7F) {
            out += (char)cp;
          } else if (cp >= 0x10000) {
            cp -= 0x10000;
            std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
            out += buf;
          } else {
            std::snprintf(buf, sizeof buf, "\\u%04x", cp);
            out += buf;
          }
        } else {
          out += (char)cp;
        }
    }
  }
  out += '"';
}

void dump_to(const Json& j, std::string& out, int indent, int level) {
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
    case Json::Str: put_string(out, j.s); break;
    case Json::Arr:
      if (j.a.empty()) {
        out += "[]";
        break;
      }
      out += '[';
      for (size_t k = 0; k < j.a.size(); k++) {
        if (k) out += indent >= 0 ? "," : ", ";
        newline(level + 1);
        dump_to(j.a[k], out, indent, level + 1);
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
        put_string(out, j.o[k].first);
        out += ": ";
        dump_to(j.o[k].second, out, indent, level + 1);
      }
      newline(level);
      out += '}';
      break;
  }
}

}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(*this, out, indent, 0);
  return out;
}

}
