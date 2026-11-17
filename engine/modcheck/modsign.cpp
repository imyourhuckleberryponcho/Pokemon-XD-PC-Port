#include "modsign.h"

#include "json.h"
#include "modsafety.h"
#include "update.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace engine::modcheck {

namespace em = engine::mods;
namespace eu = engine::update;
namespace fs = std::filesystem;

namespace {

bool FindMemberSpan(std::string_view text, std::string_view name, size_t& begin, size_t& end) {
  size_t i = 0;
  const auto skipWs = [&] {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
  };
  const auto skipString = [&](std::string* out) -> bool {
    if (i >= text.size() || text[i] != '"') return false;
    i++;
    while (i < text.size() && text[i] != '"') {
      if (text[i] == '\\') i++;
      if (out != nullptr && i < text.size()) out->push_back(text[i]);
      i++;
    }
    if (i >= text.size()) return false;
    i++;
    return true;
  };
  const auto skipValue = [&]() -> bool {
    skipWs();
    if (i >= text.size()) return false;
    if (text[i] == '"') return skipString(nullptr);
    if (text[i] == '{' || text[i] == '[') {
      int depth = 0;
      while (i < text.size()) {
        if (text[i] == '"') {
          if (!skipString(nullptr)) return false;
          continue;
        }
        if (text[i] == '{' || text[i] == '[') depth++;
        if (text[i] == '}' || text[i] == ']') {
          depth--;
          if (depth == 0) {
            i++;
            return true;
          }
        }
        i++;
      }
      return false;
    }
    while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ' ' && text[i] != '\n' && text[i] != '\r') i++;
    return true;
  };
  skipWs();
  if (i >= text.size() || text[i] != '{') return false;
  i++;
  for (;;) {
    skipWs();
    if (i >= text.size() || text[i] == '}') return false;
    std::string key;
    if (!skipString(&key)) return false;
    skipWs();
    if (i >= text.size() || text[i] != ':') return false;
    i++;
    skipWs();
    const size_t start = i;
    if (!skipValue()) return false;
    if (key == name) {
      begin = start;
      end = i;
      return true;
    }
    skipWs();
    if (i < text.size() && text[i] == ',') i++;
  }
}

std::string Escape(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char b[8];
        std::snprintf(b, sizeof b, "\\u%04x", c);
        out += b;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out + "\"";
}

bool Str(const em::json::Value& obj, const char* key, std::string& out) {
  const auto it = obj.fields.find(key);
  if (it == obj.fields.end() || it->second.kind != em::json::Value::Kind::String) return false;
  out = it->second.text;
  return true;
}

bool ReadManifestObject(const em::json::Value& m, UpdateFile& out, std::string& error) {
  if (m.kind != em::json::Value::Kind::Object) {
    error = "the update file is not one { ... } block";
    return false;
  }
  if (!Str(m, "id", out.id) || !Str(m, "version", out.version) || !Str(m, "url", out.url) || !Str(m, "sha256", out.sha256)) {
    error = "the update file is missing id, version, url or sha256";
    return false;
  }
  Str(m, "notes", out.notes);
  Str(m, "date", out.date);
  const auto size = m.fields.find("size");
  if (size == m.fields.end() || size->second.kind != em::json::Value::Kind::Number || size->second.number < 0 ||
      size->second.number > 4e12) {
    error = "the update file has no usable \"size\"";
    return false;
  }
  out.size = static_cast<uint64_t>(size->second.number);
  for (const char* list : {"games", "permissions"}) {
    const auto it = m.fields.find(list);
    if (it == m.fields.end()) continue;
    if (it->second.kind != em::json::Value::Kind::Array) {
      error = std::string("\"") + list + "\" must be a list";
      return false;
    }
    for (const em::json::Value& v : it->second.items) {
      if (v.kind != em::json::Value::Kind::String) {
        error = std::string("\"") + list + "\" must hold only texts";
        return false;
      }
      (std::string_view(list) == "games" ? out.games : out.permissions).push_back(v.text);
    }
  }
  bool hex = out.sha256.size() == 64;
  for (char c : out.sha256) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  if (!hex) {
    error = "\"sha256\" must be 64 lower case hex digits";
    return false;
  }
  if (!em::ValidModId(out.id)) {
    error = "\"id\" is not a valid mod id";
    return false;
  }
  return true;
}

bool RandomBytes(uint8_t* out, size_t n) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
  std::FILE* f = std::fopen("/dev/urandom", "rb");
  if (f == nullptr) return false;
  const bool ok = std::fread(out, 1, n, f) == n;
  std::fclose(f);
  return ok;
#endif
}

}

bool ParseUpdateFile(std::string_view text, UpdateFile& out, std::string& error) {
  out = UpdateFile{};
  if (text.size() > kMaxUpdateFileBytes) {
    error = "the update file is too large";
    return false;
  }
  em::json::Value root;
  em::json::Reader reader(text);
  if (!reader.Document(root) || root.kind != em::json::Value::Kind::Object) {
    error = "the update file is not valid JSON";
    return false;
  }
  const auto manifest = root.fields.find("manifest");
  if (manifest == root.fields.end()) {
    return ReadManifestObject(root, out, error);
  }
  std::string signature;
  if (!ReadManifestObject(manifest->second, out, error)) return false;
  if (Str(root, "signature", signature) && Str(root, "public_key", out.publicKey)) {
    out.isSigned = true;
    std::vector<uint8_t> sig, key;
    size_t begin = 0, end = 0;
    if (eu::HexDecode(signature, sig) && sig.size() == 64 && eu::HexDecode(out.publicKey, key) && key.size() == 32 &&
        FindMemberSpan(text, "manifest", begin, end)) {
      std::string message = kUpdateSignedPrefix;
      message.append(text.substr(begin, end - begin));
      out.signatureValid = eu::VerifyEd25519(key.data(), sig.data(), message);
    }
  }
  return true;
}

std::string UpdateManifestText(const UpdateFile& f) {
  std::string out = "{\n  \"format\": 1,\n";
  out += "  \"id\": " + Escape(f.id) + ",\n";
  out += "  \"version\": " + Escape(f.version) + ",\n";
  if (!f.date.empty()) out += "  \"date\": " + Escape(f.date) + ",\n";
  out += "  \"url\": " + Escape(f.url) + ",\n";
  out += "  \"sha256\": " + Escape(f.sha256) + ",\n";
  out += "  \"size\": " + std::to_string(f.size) + ",\n";
  out += "  \"notes\": " + Escape(f.notes) + ",\n";
  out += "  \"games\": [";
  for (size_t i = 0; i < f.games.size(); i++) out += (i ? ", " : "") + Escape(f.games[i]);
  out += "],\n  \"permissions\": [";
  for (size_t i = 0; i < f.permissions.size(); i++) out += (i ? ", " : "") + Escape(f.permissions[i]);
  out += "]\n}";
  return out;
}

std::string SignedUpdateText(const UpdateFile& fields, const uint8_t seed[32], std::string& publicKeyHex) {
  const std::string manifest = UpdateManifestText(fields);
  uint8_t sig[64], pub[32];
  eu::SignEd25519(seed, std::string(kUpdateSignedPrefix) + manifest, sig, pub);
  publicKeyHex = eu::HexEncode(pub, 32);
  return "{\n\"signature\": \"" + eu::HexEncode(sig, 64) + "\",\n\"public_key\": \"" + publicKeyHex + "\",\n\"manifest\": " + manifest +
         "\n}\n";
}

bool ReadKeyFile(const fs::path& file, uint8_t seed[32], std::string& publicKeyHex, std::string& error) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    error = "no signing key at " + PathText(file);
    return false;
  }
  std::string line, priv;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.rfind("private:", 0) == 0) {
      priv = line.substr(8);
      while (!priv.empty() && priv.front() == ' ') priv.erase(priv.begin());
    }
  }
  std::vector<uint8_t> bytes;
  if (!eu::HexDecode(priv, bytes) || bytes.size() != 32) {
    error = PathText(file) + " is not a signing key file (it needs a line \"private: <64 hex digits>\")";
    return false;
  }
  std::copy(bytes.begin(), bytes.end(), seed);
  uint8_t sig[64], pub[32];
  eu::SignEd25519(seed, "x", sig, pub);
  publicKeyHex = eu::HexEncode(pub, 32);
  return true;
}

bool MakeKeyFile(const fs::path& file, std::string& publicKeyHex, std::string& error) {
  std::error_code ec;
  if (fs::exists(file, ec)) {
    error = PathText(file) + " exists already; a new key would make every launcher that pinned the old one refuse your updates. "
                             "Delete it yourself if you really mean it.";
    return false;
  }
  uint8_t seed[32];
  if (!RandomBytes(seed, 32)) {
    error = "the system has no random numbers to make a key from";
    return false;
  }
  uint8_t sig[64], pub[32];
  eu::SignEd25519(seed, "x", sig, pub);
  publicKeyHex = eu::HexEncode(pub, 32);
  if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary);
  out << "# Mod update signing key. Whoever has this file can publish updates that launchers trust as yours.\r\n"
         "# Never share it, never put it in a mod or a repository. Keep a backup.\r\n"
      << "private: " << eu::HexEncode(seed, 32) << "\r\npublic: " << publicKeyHex << "\r\n";
  out.close();
  if (!out) {
    error = "could not write " + PathText(file);
    return false;
  }
  return true;
}

std::string KeyFingerprint(std::string_view publicKeyHex) {
  std::vector<uint8_t> key;
  if (!eu::HexDecode(publicKeyHex, key) || key.size() != 32) return "(no key)";
  const std::string hex = Sha256Hex(key.data(), key.size());
  std::string out;
  for (size_t i = 0; i < 16; i += 4) out += (i ? "-" : "") + hex.substr(i, 4);
  return out;
}

}
