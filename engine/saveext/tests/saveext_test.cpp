#include "saveext.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
namespace se = engine::saveext;

static int sFails = 0;
static void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    sFails++;
  }
}

static se::File Sample(uint64_t hash) {
  se::File f;
  f.saveHash = hash;
  f.writer = "test";
  f.Set({"memo", 1, false, {1, 2, 3, 4, 5}});
  f.Set({"shadow", 2, true, std::vector<uint8_t>(1000, 0xAB)});
  return f;
}

int main() {
  const se::File a = Sample(0x1122334455667788ull);
  const std::vector<uint8_t> bytes = se::Encode(a);
  se::File b;
  Check(se::Decode(bytes.data(), bytes.size(), b) == se::Decoded::Ok, "decodes");
  Check(b.saveHash == a.saveHash && b.writer == "test" && b.sections.size() == 2, "fields kept");
  Check(b.Find("shadow") && b.Find("shadow")->required && b.Find("shadow")->data.size() == 1000, "shadow section");
  Check(b.Find("memo") && b.Find("memo")->version == 1 && !b.Find("memo")->required, "memo section");
  Check(b.RequiredNames() == std::vector<std::string>{"shadow"}, "required names");
  int missed = 0;
  for (size_t i = 0; i < bytes.size(); i += 7) {
    std::vector<uint8_t> c = bytes;
    c[i] ^= 0x40;
    se::File x;
    if (se::Decode(c.data(), c.size(), x) == se::Decoded::Ok) missed++;
  }
  Check(missed == 0, "a changed byte is always caught");
  for (size_t n : {size_t(0), size_t(3), size_t(10), bytes.size() / 2, bytes.size() - 1}) {
    se::File x;
    Check(se::Decode(bytes.data(), n, x) != se::Decoded::Ok, "cut at " + std::to_string(n));
  }
  {
    std::vector<uint8_t> c = bytes;
    c[4] = 9;
    se::File x;
    Check(se::Decode(c.data(), c.size(), x) == se::Decoded::TooNew, "newer format refused");
  }
  {
    se::File e;
    e.saveHash = 5;
    const auto eb = se::Encode(e);
    se::File x;
    Check(se::Decode(eb.data(), eb.size(), x) == se::Decoded::Ok && x.sections.empty(), "empty extension");
  }

  const fs::path dir = fs::temp_directory_path() / "saveext_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  const std::vector<uint8_t> save1(5000, 1), save2(5000, 2);
  const uint64_t h1 = se::Hash64(save1.data(), save1.size()), h2 = se::Hash64(save2.data(), save2.size());
  Check(h1 != h2, "hashes differ");
  Check(se::Load(dir, "pt_x", h1).state == se::State::Missing, "missing at first");
  Check(se::Stage(dir, "pt_x", Sample(h1)), "stage");
  Check(fs::exists(se::StagedFor(dir, "pt_x")) && !fs::exists(se::FileFor(dir, "pt_x")), "staged only");
  Check(se::Commit(dir, "pt_x"), "commit");
  Check(!fs::exists(se::StagedFor(dir, "pt_x")) && fs::exists(se::FileFor(dir, "pt_x")), "committed");
  se::Loaded l = se::Load(dir, "pt_x", h1);
  Check(l.state == se::State::Paired && l.file.sections.size() == 2, "paired with its save");
  Check(se::Load(dir, "pt_x", h2).state == se::State::Stale, "another save: stale");
  Check(se::Stage(dir, "pt_x", Sample(h2)), "stage 2");
  l = se::Load(dir, "pt_x", h2);
  Check(l.state == se::State::Paired && l.promoted, "staged file promoted when the card has the new save");
  Check(se::Stage(dir, "pt_x", Sample(h1)), "stage 3");
  l = se::Load(dir, "pt_x", h2);
  Check(l.state == se::State::Paired && !l.promoted && !fs::exists(se::StagedFor(dir, "pt_x")), "stale staged file dropped, old pair kept");
  {
    std::vector<uint8_t> c = se::Encode(Sample(h2));
    c[c.size() / 2] ^= 1;
    se::WriteAtomic(se::FileFor(dir, "pt_x"), c);
    Check(se::Load(dir, "pt_x", h2).state == se::State::Damaged, "damaged file noticed");
  }
  Check(se::FileFor(dir, "a/b c").filename() == "a_b_c.ext", "key cleaned");
  Check(se::PairsWith(se::Encode(Sample(h1)), save1) && !se::PairsWith(se::Encode(Sample(h1)), save2), "PairsWith");
  {
    std::vector<uint8_t> gci1(0x40, 0xEE), gci2(0x40, 0xDD);
    gci1.insert(gci1.end(), save1.begin(), save1.end());
    gci2.insert(gci2.end(), save1.begin(), save1.end());
    const auto ext = se::Encode(Sample(h1));
    Check(se::PairsWithGci(ext, gci1) && se::PairsWithGci(ext, gci2), "PairsWithGci: the 0x40-byte header is not part of the fingerprint");
    gci2.back() ^= 1;
    Check(!se::PairsWithGci(ext, gci2) && !se::PairsWithGci(ext, std::vector<uint8_t>(10)), "PairsWithGci: changed data / too short");
  }
  {
    fs::remove_all(dir, ec);
    Check(!se::Repair(dir, "pt_r", save1, save2), "Repair: no extension, nothing done");
    Check(se::Stage(dir, "pt_r", Sample(h1)) && se::Commit(dir, "pt_r"), "Repair: an extension for save 1");
    Check(se::Repair(dir, "pt_r", save1, save2), "Repair: save 1 became save 2");
    se::Loaded r = se::Load(dir, "pt_r", h2);
    Check(r.state == se::State::Paired && r.file.sections.size() == 2 && r.file.Find("shadow") && r.file.Find("shadow")->data.size() == 1000 &&
          r.file.Find("shadow")->required, "Repair: paired with save 2, sections kept");
    Check(se::Load(dir, "pt_r", h1).state == se::State::Stale, "Repair: no longer with save 1");
    const std::vector<uint8_t> save3(5000, 3);
    Check(!se::Repair(dir, "pt_r", save1, save3) && se::Load(dir, "pt_r", h2).state == se::State::Paired,
          "Repair: an extension that did not belong to the old save is left alone");
  }
  fs::remove_all(dir, ec);
  std::printf(sFails == 0 ? "ALL PASS\n" : "%d FAILED\n", sFails);
  return sFails == 0 ? 0 : 1;
}
