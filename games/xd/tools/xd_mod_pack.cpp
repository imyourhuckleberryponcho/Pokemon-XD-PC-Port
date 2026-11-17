#include "xd_mod_rules.h"

#include <modpack.h>
#include <modsafety.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>

namespace mc = engine::modcheck;
namespace fs = std::filesystem;

namespace {
void Usage() {
  std::fprintf(stderr,
               "Usage: xd_mod_pack <mod folder> [--out <folder>] [--disc <disc image>] [--refuse-hashes <file>] [--url-base <https://...>]\n"
               "                   [--notes <text>] [--allow-same-size-textures] [--strict] [--key <key file>]\n"
               "  --key: sign the update file with your key (the private key stays in that file, outside the mod).\n"
               "  xd_mod_pack --make-key <key file> makes a new key (never overwrites one).\n"
               "  Makes <id>-<version>.zip next to the mod folder (or in --out). With --disc, files that are unchanged copies of the disc's are\n"
               "  refused too, and the deeper checks run. --refuse-hashes: a text file with one SHA-256 per line of game files that must never ship.\n"
               "  --url-base: where you will upload the zip (the update file's \"url\"); --notes: release notes for the update file.\n");
}
}

int main(int argc, char** argv) {
  mc::PackOptions options;
  std::string folder, disc, hashes;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) options.outDir = argv[++i];
    else if (a == "--disc" && i + 1 < argc) disc = argv[++i];
    else if (a == "--refuse-hashes" && i + 1 < argc) hashes = argv[++i];
    else if (a == "--url-base" && i + 1 < argc) options.urlBase = argv[++i];
    else if (a == "--notes" && i + 1 < argc) options.notes = argv[++i];
    else if (a == "--allow-same-size-textures") options.allowSameSizeTextures = true;
    else if (a == "--strict") options.strict = true;
    else if (a == "--key" && i + 1 < argc) options.signKey = fs::path(argv[++i]);
    else if (a == "--make-key" && i + 1 < argc) {
      std::string pub, error;
      if (!mc::MakeKeyFile(fs::path(argv[++i]), pub, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 3;
      }
      std::printf("Key written to %s\nPublic key: %s (fingerprint %s)\nPut the public key in mod.json as \"author_key\" if you like; keep the file secret and backed up.\n",
                  argv[i], pub.c_str(), mc::KeyFingerprint(pub).c_str());
      return 0;
    }
    else if (a == "--help" || a == "-h") {
      Usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      Usage();
      return 3;
    } else if (folder.empty()) {
      folder = a;
    } else {
      Usage();
      return 3;
    }
  }
  if (folder.empty()) {
    Usage();
    return 3;
  }
  const mc::GameRules rules = xd::modrules::Rules();
  engine::mods::LoadOptions load;
  options.check.folder = fs::path(folder);
  options.check.rules = &rules;
  if (!hashes.empty()) {
    std::ifstream in(hashes);
    std::string line;
    while (std::getline(in, line)) {
      std::string h;
      for (char c : line) {
        if (std::isxdigit(static_cast<unsigned char>(c))) h += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else break;
      }
      if (h.size() == 64) options.refuseHashes.insert(h);
    }
  }
  if (!disc.empty()) {
    std::string error;
    if (!xd::modrules::OpenDisc(disc, load, error)) {
      std::fprintf(stderr, "Could not use the disc: %s\n", error.c_str());
      return 3;
    }
    options.check.deep = &load;
    options.disc = &load.disc;
  }
  const mc::PackResult result = mc::PackMod(options);
  std::fputs(result.Format().c_str(), stdout);
  if (!disc.empty()) xd::modrules::CloseDisc();
  return result.ok ? 0 : 1;
}
