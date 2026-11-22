#include <modupdate.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace em = engine::mods;
namespace mc = engine::modcheck;
namespace mu = engine::modupdate;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void mod(const fs::path& root, const std::string& id, const std::string& extra = "", const std::string& version = "1.0") {
  write(root / id / "mod.json", "{\"id\": \"" + id + "\", \"name\": \"" + id + "\", \"version\": \"" + version + "\", \"game\": \"GXXE01\"" +
                                    (extra.empty() ? "" : ", " + extra) + "}");
}

std::vector<em::ModFolder> find(const fs::path& root) {
  std::vector<em::Message> messages;
  return em::FindMods(root, messages);
}

std::vector<const em::ModFolder*> ptrs(const std::vector<em::ModFolder>& v, std::initializer_list<const char*> ids) {
  std::vector<const em::ModFolder*> out;
  for (const char* id : ids) {
    for (const em::ModFolder& m : v) {
      if (m.manifest.id == id) out.push_back(&m);
    }
  }
  return out;
}

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
  return s;
}

void testManifestAndVersions(const fs::path& tmp) {
  check(em::CompareVersions("1.10", "1.9") > 0, "version compare: 1.10 is newer than 1.9");
  check(em::CompareVersions("2", "2.0.0") == 0, "version compare: 2 equals 2.0.0");
  check(em::CompareVersions("1.2.3", "1.2.4") < 0, "version compare: 1.2.3 older than 1.2.4");
  em::VersionNeed need{">=", "1.2"};
  check(need.SatisfiedBy("1.2") && need.SatisfiedBy("1.10") && !need.SatisfiedBy("1.1.9"), "version condition >= 1.2");

  const fs::path root = tmp / "manifest";
  mod(root, "a", "\"before\": [\"b\"], \"works_with\": [\"w\"], \"recommends\": [\"r\"], \"only_with\": [\"b\"], "
                 "\"updates\": \"https://example.org/a.update.json\", \"links\": {\"b\": \"https://example.org/b.update.json\"}, "
                 "\"author_key\": \"" + std::string(64, 'a') + "\"");
  const auto found = find(root);
  check(found.size() == 1, "mod with the new fields is found");
  if (!found.empty()) {
    const em::Manifest& m = found[0].manifest;
    check(m.before.size() == 1 && m.worksWith.size() == 1 && m.recommends.size() == 1 && m.onlyWith.size() == 1, "before / works_with / recommends / only_with are read");
    check(m.updates == "https://example.org/a.update.json" && m.links.count("b") == 1 && m.authorKey.size() == 64, "updates, links, author_key are read");
  }
  em::Manifest bad;
  std::string err;
  check(!em::ParseManifest("{\"id\":\"x\",\"game\":\"GXXE01\",\"before\":[\"bad id!\"]}", bad, err), "a bad id in \"before\" is refused");
  check(!em::ParseManifest("{\"id\":\"x\",\"game\":\"GXXE01\",\"author_key\":\"zz\"}", bad, err), "a bad author_key is refused");
  check(!em::ParseManifest("{\"id\":\"x\",\"game\":\"GXXE01\",\"links\":[1]}", bad, err), "links that is not a block is refused");
}

void testSort(const fs::path& tmp) {
  const fs::path root = tmp / "sort";
  mod(root, "base");
  mod(root, "addon", "\"requires\": [\"base\"]");
  mod(root, "early", "\"before\": [\"base\"]");
  mod(root, "late", "\"after\": [\"addon\"]", "1.0");
  mod(root, "patch", "\"only_with\": [\"base\", \"late\"]");
  const auto found = find(root);
  {
    const auto r = em::SortMods(ptrs(found, {"patch", "late", "addon", "base", "early"}));
    const auto pos = [&](const char* id) { return std::find(r.order.begin(), r.order.end(), id) - r.order.begin(); };
    check(r.cycle.empty(), "sort: no cycle");
    check(pos("base") < pos("addon") && pos("addon") < pos("late"), "sort: requires and after put the other mod first");
    check(pos("early") < pos("base"), "sort: before puts this mod first");
    check(pos("base") < pos("patch") && pos("late") < pos("patch"), "sort: a compatibility patch comes after what it patches");
    check(r.changed, "sort: reports that the order changed");
  }
  {
    const auto r = em::SortMods(ptrs(found, {"early", "base", "addon", "late", "patch"}));
    check(!r.changed && join(r.order) == "early,base,addon,late,patch", "sort: an order that already works is kept");
  }
  {
    const fs::path pr = tmp / "sortprio";
    mod(pr, "a");
    write(pr / "b" / "mod.json", "{\"id\":\"b\",\"game\":\"GXXE01\",\"priority\": -5}");
    mod(pr, "c");
    const auto f = find(pr);
    const auto r = em::SortMods(ptrs(f, {"a", "b", "c"}));
    check(join(r.order) == "b,a,c", "sort: a lower priority goes first among mods that may go either way");
  }
  {
    const fs::path cy = tmp / "sortcycle";
    mod(cy, "x", "\"after\": [\"y\"]");
    mod(cy, "y", "\"after\": [\"x\"]");
    mod(cy, "z");
    const auto f = find(cy);
    const auto r = em::SortMods(ptrs(f, {"x", "y", "z"}));
    check(r.cycle.size() == 2 && r.order.size() == 3 && r.order[0] == "z", "sort: a cycle is named, the rest is ordered");
  }
  {
    em::ModList::Profile profile;
    profile.mods = {{"patch", true}, {"gone", true}, {"base", false}};
    em::SortProfile(profile, found, nullptr);
    check(profile.mods.front().first == "base" && profile.mods.back().first == "gone" && !profile.mods.front().second,
          "sort profile: mods keep their on/off, a mod not in the folder goes last");
  }
}

void testRequirementsAndReview(const fs::path& tmp) {
  const fs::path root = tmp / "req";
  mod(root, "lib", "", "1.0");
  mod(root, "lib2", "\"requires\": [\"lib\"]");
  mod(root, "top", "\"requires\": [\"lib2\", \"ghost >= 2.0\", \"lib >= 3\"], \"links\": {\"ghost\": \"https://example.org/ghost.update.json\"}");
  mod(root, "clash", "\"conflicts\": [\"lib\"]");
  mod(root, "patchfor", "\"only_with\": [\"lib\"]");
  mod(root, "rec", "\"recommends\": [\"nothere\"], \"works_with\": [\"lib\"]");
  const auto found = find(root);
  const em::ModFolder* top = nullptr;
  for (const auto& m : found) {
    if (m.manifest.id == "top") top = &m;
  }
  check(top != nullptr, "requirements: mods found");
  if (top == nullptr) return;
  const auto r = em::ResolveRequirements(*top, found, {}, "GXXE01");
  std::vector<std::string> on;
  for (const em::ModFolder* m : r.turnOn) on.push_back(m->manifest.id);
  check(join(on) == "lib,lib2", "requirements: the needed mods are turned on, deepest first (" + join(on) + ")");
  bool ghost = false, old = false;
  for (const auto& m : r.missing) {
    if (m.id == "ghost") ghost = m.have.empty() && m.link == "https://example.org/ghost.update.json" && m.op == ">=";
    if (m.id == "lib") old = m.have == "1.0" && m.version == "3";
  }
  check(ghost, "requirements: a missing mod is named with the version it wants and its link");
  check(old, "requirements: a version that is too old says which version is installed");
  em::ModFolder clash;
  for (const auto& m : found) if (m.manifest.id == "clash") clash = m;
  const auto c = em::ResolveRequirements(clash, found, {"lib"}, "GXXE01");
  check(!c.conflicts.empty(), "requirements: a mod that is on and conflicts is named");

  em::ModList::Profile profile;
  profile.mods = {{"lib", true}, {"lib2", true}, {"top", true}, {"clash", true}, {"patchfor", true}, {"rec", true}};
  const em::Review review = em::ReviewList(profile, found, "GXXE01");
  const auto row = [&](const char* id) -> const em::ReviewRow& {
    for (const auto& x : review.rows) if (x.id == id) return x;
    return review.rows[0];
  };
  check(!row("top").problems.empty(), "review: a mod with a missing requirement is not used and says why");
  check(!row("clash").problems.empty(), "review: a conflicting mod further down is not used");
  em::ModList::Profile p2;
  p2.mods = {{"lib", true}, {"patchfor", true}, {"rec", true}};
  const em::Review r2 = em::ReviewList(p2, found, "GXXE01");
  check(r2.rows[1].problems.empty(), "review: a compatibility patch is used when what it patches is on");
  check(!r2.rows[2].suggestions.empty(), "review: recommends / works_with only suggest (the mod stays usable)");
  check(r2.rows[2].problems.empty(), "review: suggestions never stop a mod");
  em::ModList::Profile p3;
  p3.mods = {{"patchfor", true}};
  const em::Review r3 = em::ReviewList(p3, found, "GXXE01");
  check(!r3.rows[0].problems.empty(), "review: a compatibility patch without its mod is not used");
  em::ModList::Profile p4;
  p4.mods = {{"lib", true}};
  check(em::ReviewList(p4, found, "GXXJ01").rows[0].problems.size() == 1, "review: a mod for another game says so");
}

void testFine(const fs::path& tmp) {
  const fs::path root = tmp / "fine";
  mod(root, "m1", "\"scripts\": true");
  mod(root, "m2", "\"scripts\": true");
  mod(root, "m3");
  write(root / "m1" / "data" / "x.json", "{\"pokemon\": {\"#1\": {\"hp\": 5, \"atk\": 6}, \"#2\": {\"hp\": 1}}}");
  write(root / "m2" / "data" / "y.json", "{\"pokemon\": {\"#1\": {\"hp\": 9}, \"#2\": {\"def\": 1}}}");
  write(root / "m3" / "data" / "z.json", "{\"pokemon\": {\"#1\": {\"spd\": 9}}}");
  write(root / "m1" / "rooms" / "side_room" / "a.bin", "x");
  write(root / "m2" / "objects" / "side_room.json", "{}");
  write(root / "m1" / "scripts" / "a.lua", "on(\"battle.damage\", function() end)\n-- on(\"turn_end\"");
  write(root / "m2" / "scripts" / "b.lua", "local x = 1\non('battle.damage', function() end)");
  const auto found = find(root);
  const auto c = em::FineConflicts(ptrs(found, {"m1", "m2", "m3"}));
  int data = 0, rooms = 0, hooks = 0;
  bool hpFound = false, wrongField = false;
  for (const auto& x : c) {
    data += x.kind == em::FineConflict::Kind::DataField;
    rooms += x.kind == em::FineConflict::Kind::StoryRoom;
    hooks += x.kind == em::FineConflict::Kind::LuaHook;
    if (x.what == "pokemon / #1 / hp") hpFound = x.earlier == "m1" && x.later == "m2";
    if (x.what.find("atk") != std::string::npos || x.what.find("#2") != std::string::npos || x.what.find("spd") != std::string::npos) wrongField = true;
  }
  check(data == 1 && hpFound, "fine conflicts: the same table row and field is found, later mod wins");
  check(!wrongField, "fine conflicts: other fields of the same row, and other rows, are no clash");
  check(rooms == 1, "fine conflicts: the same story room (rooms/ and objects/) is found");
  check(hooks == 1, "fine conflicts: the same script hook is found (a commented-out one is not counted by the hint? " + std::to_string(hooks) + ")");
  em::ModList::Profile profile;
  profile.mods = {{"m1", true}, {"m2", true}, {"m3", true}};
  const em::Review review = em::ReviewList(profile, found, "GXXE01");
  check(!review.rows[0].warnings.empty() && !review.rows[1].warnings.empty(), "review: both mods get the finer warning, saying which wins");
  check(review.rows[2].warnings.empty(), "review: an unrelated mod gets none");
}

void testSigning(const fs::path& tmp) {
  std::string pub, error;
  const fs::path keyFile = tmp / "keys" / "author.key";
  check(mc::MakeKeyFile(keyFile, pub, error) && pub.size() == 64, "signing: a key file is made");
  check(!mc::MakeKeyFile(keyFile, pub, error), "signing: an existing key file is never overwritten");
  uint8_t seed[32];
  std::string pub2;
  check(mc::ReadKeyFile(keyFile, seed, pub2, error) && pub2 == pub, "signing: the key file reads back with the same public key");
  mc::UpdateFile f;
  f.id = "mymod";
  f.version = "1.1";
  f.url = "https://example.org/mymod-1.1.zip";
  f.sha256 = std::string(64, 'b');
  f.size = 1234;
  f.notes = "New \"things\"\nline two";
  f.games = {"GXXE01"};
  f.permissions = {"changes-tables"};
  std::string signedText = mc::SignedUpdateText(f, seed, pub2);
  mc::UpdateFile g;
  check(mc::ParseUpdateFile(signedText, g, error) && g.isSigned && g.signatureValid && g.notes == f.notes && g.permissions == f.permissions, "signing: a signed update file reads back and its signature is valid");
  std::string tampered = signedText;
  const size_t at = tampered.find("1234");
  tampered.replace(at, 4, "1235");
  mc::UpdateFile t;
  check(mc::ParseUpdateFile(tampered, t, error) && t.isSigned && !t.signatureValid, "signing: one changed digit makes the signature invalid");
  std::string tampered2 = signedText;
  tampered2.replace(tampered2.find("New"), 3, "Old");
  check(mc::ParseUpdateFile(tampered2, t, error) && !t.signatureValid, "signing: changed notes make the signature invalid");
  uint8_t other[32];
  for (int i = 0; i < 32; i++) other[i] = static_cast<uint8_t>(i + 1);
  std::string otherPub;
  const std::string otherText = mc::SignedUpdateText(f, other, otherPub);
  mc::UpdateFile o;
  check(mc::ParseUpdateFile(otherText, o, error) && o.signatureValid && o.publicKey == otherPub && otherPub != pub, "signing: another key also signs validly (the PIN is what refuses it)");
  std::string swapped = signedText;
  swapped.replace(swapped.find(pub), pub.size(), otherPub);
  check(mc::ParseUpdateFile(swapped, t, error) && !t.signatureValid, "signing: a signature with someone else's public key pasted in is invalid");
  mc::UpdateFile u;
  const std::string unsignedText = mc::UpdateManifestText(f) + "\n";
  check(mc::ParseUpdateFile(unsignedText, u, error) && !u.isSigned, "signing: an unsigned update file reads, flagged unsigned");

  using mu::Trust;
  check(mu::EvaluateTrust(g, "mymod", "", "").trust == Trust::FirstUse, "trust: no pinned key = first use (the key will be pinned)");
  check(mu::EvaluateTrust(g, "mymod", pub, "").trust == Trust::Trusted, "trust: the pinned key signs = trusted");
  const auto otherKey = mu::EvaluateTrust(o, "mymod", pub, "");
  check(otherKey.trust == Trust::NeedsConfirm && otherKey.text.find("DIFFERENT key") != std::string::npos && otherKey.oldKey == pub && otherKey.newKey == otherPub &&
            !mu::PinsKey(otherKey.trust),
        "trust: another key than the pinned one, no announcement: needs the player's explicit confirmation (never silent), with a plain reason");
  const auto noSig = mu::EvaluateTrust(u, "mymod", pub, "");
  check(noSig.trust == Trust::Refused && noSig.text.find("isn't signed") != std::string::npos, "trust: no signature is refused, with a plain reason");
  check(mu::EvaluateTrust(t, "mymod", "", "").trust == Trust::Refused, "trust: a bad signature is refused");
  check(mu::EvaluateTrust(g, "othermod", "", "").trust == Trust::Refused, "trust: an update file for another mod is refused");
  check(mu::EvaluateTrust(g, "mymod", "", otherPub).trust == Trust::NeedsConfirm, "trust: a key different from the mod's own author_key needs the player's confirmation");
  check(mu::EvaluateTrust(g, "mymod", "", pub).trust == Trust::FirstUse, "trust: the mod's author_key equal to the signer is accepted");
  check(mc::KeyFingerprint(pub).size() == 19, "signing: the fingerprint has four groups of four");

  {
    const fs::path newKeyFile = tmp / "keys" / "author2.key", thirdKeyFile = tmp / "keys" / "author3.key";
    std::string message;
    check(mc::RotateKeyFile(keyFile, newKeyFile, message, error) && fs::exists(newKeyFile) && message.find("From now on pack with --key") != std::string::npos,
          "rotate: --rotate-key makes the new key file (when it is missing) and says what to do next");
    uint8_t seed2[32];
    std::string pubNew;
    check(mc::ReadKeyFile(newKeyFile, seed2, pubNew, error) && pubNew != pub, "rotate: the new key file is another key and still reads as a key file");
    const std::vector<mc::KeyChange> changes = mc::ReadKeyChanges(newKeyFile);
    check(changes.size() == 1 && changes[0].oldKey == pub && changes[0].newKey == pubNew && mc::VerifyKeyChange(changes[0]), "rotate: the statement is in the new key file and verifies");
    check(mc::ReadKeyChanges(keyFile).empty(), "rotate: the old key file is not changed");
    check(!mc::RotateKeyFile(keyFile, keyFile, message, error), "rotate: a key cannot be rotated into itself");
    mc::KeyChange forged = changes[0];
    forged.newKey = otherPub;
    check(!mc::VerifyKeyChange(forged), "rotate: a statement with the new key changed does not verify");
    mc::KeyChange stranger;
    std::string why;
    check(mc::MakeKeyChange(other, pubNew, stranger, why) && mc::VerifyKeyChange(stranger) && stranger.oldKey == otherPub, "rotate: anyone can sign a statement; it only counts for the key that signed it");

    const std::string changed = mc::SignedUpdateText(f, seed2, pubNew, changes);
    mc::UpdateFile c;
    check(mc::ParseUpdateFile(changed, c, error) && c.signatureValid && c.publicKey == pubNew && c.keyChanges.size() == 1 && c.keyChanges[0].oldKey == pub,
          "rotate: the update file carries the statement next to the signature and reads back");
    const auto planned = mu::EvaluateTrust(c, "mymod", pub, "");
    check(planned.trust == Trust::KeyChanged && mu::PinsKey(planned.trust) && planned.oldKey == pub && planned.newKey == pubNew && planned.text.find("as planned") != std::string::npos,
          "rotate: a launcher that pinned the old key accepts the new key by itself (and will keep it)");
    check(mu::EvaluateTrust(c, "mymod", pubNew, "").trust == Trust::Trusted, "rotate: a launcher that already pinned the new key: trusted as usual");
    check(mu::EvaluateTrust(c, "mymod", "", pub).trust == Trust::KeyChanged, "rotate: the installed mod.json's author_key (the old key) follows the announcement too");
    check(mu::EvaluateTrust(c, "mymod", otherPub, "").trust == Trust::NeedsConfirm, "rotate: the statement does not help a launcher that pinned some other key");
    const std::string bare = mc::SignedUpdateText(f, seed2, pubNew);
    mc::UpdateFile b;
    check(mc::ParseUpdateFile(bare, b, error) && b.signatureValid && b.keyChanges.empty(), "lost key: an update signed with a new key and no statement reads fine");
    const auto lost = mu::EvaluateTrust(b, "mymod", pub, "");
    check(lost.trust == Trust::NeedsConfirm && !mu::PinsKey(lost.trust) && lost.text.find(mc::KeyFingerprint(pubNew)) != std::string::npos &&
              lost.text.find("Nothing is installed until you confirm") != std::string::npos,
          "lost key: the launcher shows the new key's fingerprint and waits for the player (never switches by itself)");
    mc::UpdateFile forgedFile = c;
    forgedFile.keyChanges[0].signature[0] = forgedFile.keyChanges[0].signature[0] == 'a' ? 'b' : 'a';
    check(mu::EvaluateTrust(forgedFile, "mymod", pub, "").trust == Trust::NeedsConfirm, "lost key: a statement with a damaged signature counts for nothing");
    check(mc::RotateKeyFile(newKeyFile, thirdKeyFile, message, error), "rotate: a second rotation (new -> third) works");
    uint8_t seed3[32];
    std::string pubThird;
    mc::ReadKeyFile(thirdKeyFile, seed3, pubThird, error);
    const std::vector<mc::KeyChange> chain = mc::ReadKeyChanges(thirdKeyFile);
    check(chain.size() == 2, "rotate: the third key file also holds the earlier statement (a launcher several keys behind follows)");
    mc::UpdateFile d;
    mc::ParseUpdateFile(mc::SignedUpdateText(f, seed3, pubThird, chain), d, error);
    check(mu::EvaluateTrust(d, "mymod", pub, "").trust == Trust::KeyChanged && mu::EvaluateTrust(d, "mymod", pubNew, "").trust == Trust::KeyChanged,
          "rotate: a chain of statements leads from either earlier key to the newest");
    mc::UpdateFile e;
    mc::ParseUpdateFile(mc::SignedUpdateText(f, seed3, pubThird, {chain[1]}), e, error);
    check(mu::EvaluateTrust(e, "mymod", pub, "").trust == Trust::NeedsConfirm && mu::EvaluateTrust(e, "mymod", pubNew, "").trust == Trust::KeyChanged,
          "rotate: with a link missing from the chain the older launcher is asked, the nearer one is not");
    check(mc::KeyChainReaches({}, pub, pub) && !mc::KeyChainReaches({}, pub, pubNew), "rotate: a key reaches itself; nothing reaches another key without statements");
  }

  mu::Folders folders;
  folders.stateFolder = tmp / "state";
  folders.modsFolder = tmp / "mods";
  mu::KeyStore store;
  store.keys["mymod"] = pub;
  check(mu::SaveKeys(folders, store) && mu::LoadKeys(folders).Pinned("MyMod") == pub, "pinned keys: saved and read back (ids compare without case)");
  mu::Choices c;
  c.asked = true;
  c.always = true;
  c.skipped["mymod"] = "1.1";
  mu::SaveChoices(folders, c);
  const mu::Choices c2 = mu::LoadChoices(folders);
  check(c2.asked && c2.always && c2.skipped.count("mymod") == 1 && c2.skipped.at("mymod") == "1.1", "choices: asked, always and skipped versions are kept");
}

void testUrls() {
  std::string why;
  check(mu::UrlAllowed("https://example.org/a.json", why), "urls: https is allowed");
  check(!mu::UrlAllowed("http://example.org/a.json", why), "urls: http is refused");
  check(!mu::UrlAllowed("file:///C:/x.json", why), "urls: file:// is refused");
  check(!mu::UrlAllowed("https://user:pw@example.org/a.json", why), "urls: a password in the address is refused");
  check(!mu::UrlAllowed("ftp://example.org/a", why), "urls: other schemes are refused");
  check(!mu::UrlAllowed("http://127.0.0.1:8000/a.json", why), "urls: local http is refused without the test switch");
  check(mu::HostOf("https://example.org:8443/x/y") == "example.org:8443", "urls: the host is named for the question");
}

void testPermissions() {
  const auto added = mu::AddedPermissions({"changes-tables"}, {"changes-tables", "scripts", "scripts", "replaces-files"});
  check(added.size() == 2 && added[0] == "scripts" && added[1] == "replaces-files", "permissions: only the new ones are named, once each");
  check(mu::AddedPermissions({"scripts"}, {"scripts"}).empty(), "permissions: nothing added when equal");
  check(mu::PermissionText("scripts").find("Lua") != std::string::npos, "permissions: codes have plain words");
}

void testProtection(const fs::path& tmp) {
  const fs::path a = tmp / "prot" / "a", b = tmp / "prot" / "b", c = tmp / "prot" / "c", d = tmp / "prot" / "d", e = tmp / "prot" / "e";
  const std::string add1 = "{\"add\": {\"species\": {\"Zorb\": {\"copy\": \"Eevee\"}, \"Blat\": {\"copy\": \"Eevee\"}}}}";
  for (const fs::path& p : {a, b, c, d, e}) mod(p.parent_path(), p.filename().string());
  write(a / "data" / "rows.json", add1);
  write(b / "data" / "rows.json", add1);
  write(b / "textures" / "x.png", "new picture");
  check(!mu::Analyse(a, b).worldChanged, "protection: a looks-only change (a texture) doesn't touch playthroughs");
  write(c / "data" / "rows.json", "{\"add\": {\"species\": {\"Zorb\": {\"copy\": \"Eevee\"}, \"Blat\": {\"copy\": \"Eevee\"}, \"Newone\": {\"copy\": \"Eevee\"}}}}");
  const auto appended = mu::Analyse(a, c);
  check(appended.worldChanged && !appended.addedRowsBreak, "protection: a row added at the end changes the world but doesn't break old rows");
  write(d / "data" / "rows.json", "{\"add\": {\"species\": {\"Blat\": {\"copy\": \"Eevee\"}, \"Zorb\": {\"copy\": \"Eevee\"}}}}");
  const auto reordered = mu::Analyse(a, d);
  check(!reordered.addedRowsBreak && reordered.worldChanged && !reordered.reasons.empty(), "protection: reordered added Pokemon keep their labels: not a break (Continue remaps by label), still a world change");
  const fs::path f = tmp / "prot" / "f", g = tmp / "prot" / "g";
  for (const fs::path& p : {f, g}) mod(p.parent_path(), p.filename().string());
  write(f / "data" / "rows.json", "{\"add\": {\"waza_data\": {\"X\": {\"copy\": 1}, \"Y\": {\"copy\": 1}}}}");
  write(g / "data" / "rows.json", "{\"add\": {\"waza_data\": {\"X2\": {\"copy\": 1, \"label\": \"x\"}, \"Y\": {\"copy\": 1}}}}");
  check(!mu::Analyse(f, g).addedRowsBreak, "protection: a renamed row that keeps its label (\"label\": \"x\" = the old slug) is not a break");
  write(g / "data" / "rows.json", "{\"add\": {\"waza_data\": {\"X2\": {\"copy\": 1}, \"Y\": {\"copy\": 1}}}}");
  check(mu::Analyse(f, g).addedRowsBreak, "protection: a row renamed with no \"label\" is a different label: breaks");
  write(f / "data" / "rows.json", "{\"add\": {\"flag_data\": {\"X\": {}, \"Y\": {}}}}");
  write(g / "data" / "rows.json", "{\"add\": {\"flag_data\": {\"Y\": {}, \"X\": {}}}}");
  check(mu::Analyse(f, g).addedRowsBreak, "protection: tables without labels (flags) are numbered by order: reordering still breaks");
  write(f / "mod.json", "{\"id\": \"f\", \"name\": \"F\", \"version\": \"1.0\", \"game\": \"GXXE01\", \"label_namespace\": \"a.b\"}");
  write(g / "mod.json", "{\"id\": \"f\", \"name\": \"F\", \"version\": \"1.1\", \"game\": \"GXXE01\", \"label_namespace\": \"a.c\"}");
  write(f / "data" / "rows.json", "{\"add\": {\"moves\": {\"X\": {\"copy\": 1}}}}");
  write(g / "data" / "rows.json", "{\"add\": {\"moves\": {\"X\": {\"copy\": 1}}}}");
  check(mu::Analyse(f, g).addedRowsBreak, "protection: a changed label_namespace makes every row a new row: breaks");
  write(e / "data" / "rows.json", "{\"add\": {\"species\": {\"Zorb\": {\"copy\": \"Eevee\"}}}}");
  check(mu::Analyse(a, e).addedRowsBreak, "protection: a removed row breaks saves that hold it");
  write(e / "data" / "rows.json", "{\"pokemon\": {\"#1\": {\"hp\": 3}}}");
  write(a / "data" / "rows.json", "{\"pokemon\": {\"#1\": {\"hp\": 4}}}");
  const auto numbers = mu::Analyse(a, e);
  check(numbers.worldChanged && !numbers.addedRowsBreak && !numbers.reasons.empty(), "protection: changed numbers are a world change, with a reason in words");
  check(mu::PlaythroughsUsing({{"Save 1", tmp / "nowhere"}}, "x").empty(), "protection: no setup files = no playthrough uses the mod");
  {
    const fs::path root = tmp / "numbers_card";
    write(root / "playthroughs" / "Card A" / "pt_1.json",
          "{\"format\": 1, \"engine\": \"faithful\", \"mods\": [{\"id\": \"tower\", \"name\": \"Tower\", \"version\": \"1.0\", \"applies\": \"any_time\"}], "
          "\"mod_data\": {\"tower\": {\"version\": \"1.0\", \"values\": {\"streak\": 48}}}, \"save\": {\"trainer\": \"Ada\", \"trainer_id\": 1, \"secret_id\": 2}}");
    const auto users = mu::PlaythroughsUsing({{"Save 1", root}}, "tower");
    check(users.size() == 1 && users[0].keepsNumbers && users[0].version == "1.0" && users[0].label == "Ada", "protection: a save that keeps a script mod's numbers uses the mod, with the version that wrote them");
    check(mu::PlaythroughsUsing({{"Save 1", root}}, "other").empty(), "protection: another mod is not a user");
  }
}

void testInstall(const fs::path& tmp) {
  mu::Folders f;
  f.modsFolder = tmp / "inst" / "mods";
  f.stateFolder = tmp / "inst" / "state";
  mod(f.modsFolder, "tool", "", "1.0");
  write(f.modsFolder / "tool" / "data" / "old.json", "{}");
  mu::Staged staged;
  staged.ok = true;
  staged.root = f.Temp() / "one";
  staged.folder = staged.root / "mod";
  mod(staged.root, "mod", "", "2.0");
  write(staged.folder / "mod.json", "{\"id\":\"tool\",\"name\":\"tool\",\"version\":\"2.0\",\"game\":\"GXXE01\"}");
  write(staged.folder / "data" / "new.json", "{}");
  check(!mu::Previous(f, "tool").exists, "install: nothing kept before the first update");
  const auto r = mu::Install(f, staged, "tool", f.modsFolder / "tool", std::string(64, 'c'));
  check(r.ok && r.replaced, "install: the update is put in place");
  check(fs::exists(f.modsFolder / "tool" / "data" / "new.json") && !fs::exists(f.modsFolder / "tool" / "data" / "old.json"), "install: the mod folder holds the new version");
  const auto prev = mu::Previous(f, "tool");
  check(prev.exists && prev.version == "1.0", "install: the old version is kept (1.0)");
  check(mu::LoadKeys(f).Pinned("tool") == std::string(64, 'c'), "install: the author's key is pinned");
  std::string error;
  check(mu::RollBack(f, "tool", f.modsFolder / "tool", error), "roll back: works");
  check(fs::exists(f.modsFolder / "tool" / "data" / "old.json") && !fs::exists(f.modsFolder / "tool" / "data" / "new.json"), "roll back: the old files are back");
  check(mu::Previous(f, "tool").version == "2.0", "roll back: the newer version is kept, so going forward again is possible");
  check(mu::RollBack(f, "tool", f.modsFolder / "tool", error) && fs::exists(f.modsFolder / "tool" / "data" / "new.json"), "roll back twice: forward again");
  check(!mu::RollBack(f, "other", f.modsFolder / "other", error), "roll back: nothing kept = a plain refusal");
  mu::Staged s2;
  s2.ok = true;
  s2.root = f.Temp() / "two";
  s2.folder = s2.root / "mod";
  write(s2.folder / "mod.json", "{\"id\":\"newmod\",\"version\":\"1\",\"game\":\"GXXE01\"}");
  const auto r2 = mu::Install(f, s2, "newmod", {}, "");
  check(r2.ok && !r2.replaced && fs::exists(f.modsFolder / "newmod" / "mod.json"), "install: a new mod goes into a folder named after its id");
  check(!fs::exists(s2.root), "install: the temporary folder is gone");
  mu::Staged s3;
  s3.ok = true;
  s3.root = f.Temp() / "three";
  s3.folder = s3.root / "mod";
  write(s3.folder / "mod.json", "{}");
  check(!mu::Install(f, s3, "newmod", {}, "").ok, "install: never on top of a folder that is there already");
}

void testShare(const fs::path& tmp) {
  const fs::path root = tmp / "share";
  mod(root, "one", "\"updates\": \"https://example.org/one.update.json\"", "1.2");
  mod(root, "two", "", "3.0");
  mod(root, "three");
  const auto found = find(root);
  em::ModList::Profile profile;
  profile.mods = {{"two", true}, {"one", true}, {"three", false}};
  const std::string text = mu::ExportList(profile, found, "my run", "GXXE01");
  check(text.find("three") == std::string::npos && text.find(root.string()) == std::string::npos && text.find("\\\\") == std::string::npos,
        "share: off mods and local paths are not in the export");
  mu::SharedList list;
  std::string error;
  check(mu::ParseList(text, list, error) && list.mods.size() == 2 && list.mods[0].id == "two" && list.mods[1].version == "1.2", "share: the export reads back, in order, with versions");
  const fs::path other = tmp / "share2";
  mod(other, "one", "", "1.0");
  const auto found2 = find(other);
  const auto plan = mu::PlanImport(list, found2, "GXXE01");
  check(plan.rows.size() == 2 && plan.rows[0].state == mu::ImportRow::State::Missing && plan.rows[1].state == mu::ImportRow::State::OtherVersion,
        "share: import names what is missing and what is in another version");
  check(plan.rows[1].link.empty() && plan.rows[0].link.empty(), "share: no link when the list's and the mod's own ones are absent");
  const auto planWithLink = mu::PlanImport(list, found, "GXXE01");
  check(planWithLink.rows[1].state == mu::ImportRow::State::Have, "share: the same version is 'have'");
  em::ModList::Profile mine;
  mine.mods = {{"zzz", true}, {"one", false}};
  const auto missing = mu::ApplyImport(mine, plan);
  check(missing.size() == 1 && missing[0] == "two" && mine.mods[0].first == "one" && mine.mods[0].second && mine.mods[1].first == "zzz",
        "share: applying puts the shared mods first, on, in the list's order, and names the missing ones");
  mu::SharedList bad;
  check(!mu::ParseList("{\"kind\": \"something\"}", bad, error) && !mu::ParseList("not json", bad, error), "share: a file that isn't a mod list is refused");
  check(!mu::ParseList("{\"kind\":\"xd-port-mod-list\",\"mods\":[{\"id\":\"../evil\"}]}", bad, error), "share: a bad mod id in a list is refused");
  const std::string http = "{\"kind\":\"xd-port-mod-list\",\"mods\":[{\"id\":\"far\",\"updates\":\"http://example.org/far.update.json\"}]}";
  mu::SharedList l3;
  check(mu::ParseList(http, l3, error) && mu::PlanImport(l3, found2, "GXXE01").rows[0].link.empty(), "share: an http link in a list is not offered");
  const auto wrongGame = mu::PlanImport(list, found2, "GXXJ01");
  check(!wrongGame.gameNote.empty(), "share: a list for another game says so");
}

}

int main(int argc, char** argv) {
  const fs::path tmp = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "modupdate_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp);
  testManifestAndVersions(tmp);
  testSort(tmp);
  testRequirementsAndReview(tmp);
  testFine(tmp);
  testSigning(tmp);
  testUrls();
  testPermissions();
  testProtection(tmp);
  testInstall(tmp);
  testShare(tmp);
  fs::remove_all(tmp, ec);
  std::printf("%s\n", sOk ? "ALL OK" : "FAILED");
  return sOk ? 0 : 1;
}
