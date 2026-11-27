#include "xd_pc_boxes.h"
#include "xd_save_ext.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace xd::saveext {
static Handler sHandler;
void Register(const Handler& handler) { sHandler = handler; }
}

namespace px = xd::pcboxes;

static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

static void NameBox(uint16_t* dst, int n) {
  std::memset(dst, 0, px::kNameChars * 2);
  const char* t = "BOX ";
  int i = 0;
  for (; t[i]; i++) dst[i] = static_cast<uint16_t>(t[i]);
  if (n >= 10) dst[i++] = static_cast<uint16_t>('0' + n / 10);
  dst[i++] = static_cast<uint16_t>('0' + n % 10);
}
static void ClearBox(uint8_t* p, int count) { std::memset(p, 0, static_cast<size_t>(count) * px::kPokemonBytes); }

int main() {
  px::SetInitFunctions(NameBox, ClearBox);
  px::Forget();
  check(px::Count() == 8 && px::SaveSection().empty(), "plain game: 8 boxes, nothing to keep");
  check(px::ExtraBox(8) == nullptr, "no box 9 yet");
  px::Configure(0);
  px::Configure(8);
  px::Configure(40);
  check(px::Count() == 8, "0, 8 and 40 are not a box count: still 8");
  px::Configure(12);
  check(px::Count() == 12 && px::ExtraBox(11) != nullptr && px::ExtraBox(12) == nullptr, "12 boxes");
  check(px::ExtraBox(8)->name[4] == '9' && px::ExtraBox(11)->name[4] == '1' && px::ExtraBox(11)->name[5] == '2', "named BOX 9 .. BOX 12");
  px::Configure(10);
  check(px::Count() == 12, "never lowered");
  check(!px::Raise(12) && !px::Raise(33) && px::Raise(14) && px::Count() == 14, "raise: only upwards, at most 32");

  px::Box* b = px::ExtraBox(9);
  for (size_t i = 0; i < px::kPokemonBytes; i++) b->pokemon[3][i] = static_cast<uint8_t>(i * 7 + 1);
  px::ExtraBox(8)->name[0] = 'X';
  px::SetLastView(13);
  const std::vector<uint8_t> a = px::SaveSection();
  check(a.size() == 2 + 6 * px::kBoxBytes && a[0] == 14 && a[1] == 13, "section: 14 boxes, last view 13, 6 extra boxes");
  check(a[2] == 0 && a[3] == 'X', "box names are in the card's (big-endian) byte order");

  px::Forget();
  px::SetInitFunctions(NameBox, ClearBox);
  check(px::LoadSection(a.data(), a.size(), 1) && px::Count() == 14, "loaded: 14 boxes again");
  check(px::ExtraBox(8)->name[0] == 'X' && px::LastView() == 13, "name and last view back");
  px::Box* c = px::ExtraBox(9);
  bool same = true;
  uint8_t ref[px::kPokemonBytes];
  for (size_t i = 0; i < px::kPokemonBytes; i++) ref[i] = static_cast<uint8_t>(i * 7 + 1);
  same = std::memcmp(c->pokemon[3], ref, sizeof ref) == 0;
  check(same, "the Pokemon came back byte for byte");
  check(px::SaveSection() == a, "saved again: the same bytes");

  px::Forget();
  px::SetInitFunctions(NameBox, ClearBox);
  px::Configure(20);
  check(px::LoadSection(a.data(), a.size(), 1) && px::Count() == 20, "setup says 20, extension 14: 20 boxes");
  check(px::ExtraBox(19)->name[5] == '0' && px::ExtraBox(9)->pokemon[3][0] == 1, "new boxes named, old Pokemon kept");
  px::Forget();
  px::SetInitFunctions(NameBox, ClearBox);
  px::Configure(0);
  check(px::LoadSection(a.data(), a.size(), 1) && px::Count() == 14, "no setup number, extension 14: 14 boxes");
  std::vector<uint8_t> bad = a;
  bad.pop_back();
  check(!px::LoadSection(bad.data(), bad.size(), 1) && !px::LoadSection(a.data(), a.size(), 2), "a damaged / newer section is refused");
  px::ResetSection();
  check(px::ExtraBox(9)->pokemon[3][0] == 0, "reset: empty boxes");
  std::printf("%s\n", failures == 0 ? "ALL OK" : "FAILED");
  return failures == 0 ? 0 : 1;
}
