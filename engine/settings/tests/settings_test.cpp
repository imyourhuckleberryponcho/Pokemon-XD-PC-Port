#include <settings.h>
#include <settings_c.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>
#include <iterator>
#include <string>
#include <vector>

using namespace engine::settings;
namespace fs = std::filesystem;

namespace {

bool sOk = true;
std::vector<std::string> sWarnings;

void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

void setup(Registry& r) {
  r.SetWarningSink([](const std::string& m) { sWarnings.push_back(m); });
  RegisterStandardSettings(r);
  r.Add(Definition::Float("game.walk_speed", Category::Gameplay, "Walk speed", "Test float.", 0.5f, 4.0f, 1.0f, 1.0f));
  r.Add(Definition::Int("game.level_cap", Category::Gameplay, "Level cap", "Test int.", 1, 100, 100, 100));
}

void write_file(const fs::path& folder, const std::string& text) {
  fs::create_directories(folder);
  std::ofstream(folder / Registry::kFileName, std::ios::binary) << text;
}

std::string read_file(const fs::path& folder) {
  std::ifstream in(folder / Registry::kFileName, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool all_values_equal(const Registry& a, const Registry& b) {
  if (a.Count() != b.Count()) {
    return false;
  }
  for (size_t i = 0; i < a.Count(); i++) {
    if (a.ValueText(i) != b.ValueText(b.Find(a.Get(i).id))) {
      return false;
    }
  }
  return true;
}

void test_presets() {
  Registry r;
  setup(r);
  const BoolSetting widescreen = r.Bool("video.widescreen");
  const EnumSetting aa = r.Enum("video.anti_aliasing");
  const IntSetting music = r.Int("audio.music_volume");

  check(sWarnings.empty(), "standard settings register without warnings");
  if (r.Count() != 46) std::printf("     (%zu registered)\n", r.Count());
  check(r.Count() == 46, "44 standard settings + 2 test ones registered");
  check(widescreen.Valid() && aa.Valid() && music.Valid(), "handles found");
  check(!r.Bool("video.anti_aliasing").Valid() && !r.Bool("no.such").Valid(), "wrong type / unknown id: no handle");
  check(!r.Bool("no.such").Get(), "an invalid handle reads as off");

  check(r.CurrentPreset() == Preset::Original && r.IsOriginal(), "starts as Original");
  bool allOriginal = true;
  for (size_t i = 0; i < r.Count(); i++) {
    const Value v = r.ValueOf(i), o = r.Get(i).original;
    allOriginal &= r.Get(i).type == Type::Float ? v.f == o.f : v.i == o.i;
  }
  check(allOriginal, "every value is its Original default");
  check(!widescreen.Get() && aa.Get() == r.ChoiceIndex("video.anti_aliasing", "off"), "Original: 4:3, no AA");
  check(r.Bool("video.original_fog_bloom").Get(), "Original keeps the original fog and bloom");

  const uint32_t before = r.Changes();
  r.ApplyPreset(Preset::Enhanced);
  check(r.CurrentPreset() == Preset::Enhanced && !r.IsOriginal(), "Enhanced is not Original");
  check(widescreen.Get() && aa.Get() == r.ChoiceIndex("video.anti_aliasing", "4x"), "Enhanced: widescreen, 4x AA");
  check(r.Changes() != before, "change counter moved");

  check(r.SetEnum("video.anti_aliasing", "8x"), "set AA to 8x");
  check(r.CurrentPreset() == Preset::Custom && r.BasePreset() == Preset::Enhanced, "now Custom, based on Enhanced");
  check(widescreen.Get(), "other values kept from Enhanced");
  check(r.DefaultOf(r.Find("video.widescreen"), Preset::Custom).i == 1, "Custom's defaults are its base preset's");

  check(!r.SetInt("video.resolution_scale", 99) && !r.SetEnum("video.anti_aliasing", "3x") &&
            !r.SetFloat("game.walk_speed", 0.0f) && !r.SetBool("video.anti_aliasing", true),
        "out-of-range / unknown-choice / wrong-type values refused");
  check(aa.Get() == r.ChoiceIndex("video.anti_aliasing", "8x"), "refused value left the old one");

  r.ApplyPreset(Preset::Original);
  check(r.CurrentPreset() == Preset::Original && r.IsOriginal() && !widescreen.Get(), "Original preset restores all");
  r.SetBool("video.widescreen", true);
  check(r.CurrentPreset() == Preset::Custom && r.BasePreset() == Preset::Original && !r.IsOriginal(),
        "a change from Original: Custom, not Original");
  r.SetBool("video.widescreen", false);
  check(r.CurrentPreset() == Preset::Custom && r.IsOriginal(), "changed back: still Custom, but IsOriginal");

  r.ApplyPreset(Preset::Original);
  check(r.SetInt("audio.music_volume", 40) && music.Get() == 40, "music volume set");
  check(r.CurrentPreset() == Preset::Original && r.IsOriginal(), "a preference keeps Original");
  r.ApplyPreset(Preset::Enhanced);
  check(music.Get() == 40, "presets leave preferences alone");

  r.ApplyPreset(Preset::Custom);
  check(r.CurrentPreset() == Preset::Custom && r.BasePreset() == Preset::Enhanced && widescreen.Get(),
        "choosing Custom keeps the values");
}

void test_registration() {
  Registry r;
  setup(r);
  sWarnings.clear();
  check(!r.Add(Definition::Bool("video.widescreen", Category::Video, "x", "x", false, false)), "duplicate id refused");
  check(!r.Add(Definition::Bool("Bad Id", Category::Video, "x", "x", false, false)), "malformed id refused");
  check(!r.Add(Definition::Bool("nodot", Category::Video, "x", "x", false, false)), "id without a section refused");
  check(!r.Add(Definition::Int("game.x", Category::Video, "x", "x", 0, 10, 11, 0)), "default out of range refused");
  check(!r.Add(Definition::Enum("game.y", Category::Video, "x", "x", {{"a", "A"}}, "a", "b")), "unknown choice refused");
  Definition pref = Definition::Int("game.z", Category::Audio, "x", "x", 0, 10, 1, 2);
  pref.preference = true;
  check(!r.Add(pref), "preference with different preset defaults refused");
  check(sWarnings.size() == 6, "each refusal warned");

  r.ApplyPreset(Preset::Enhanced);
  r.Add(Definition::Bool("game.late", Category::Gameplay, "Late", "Registered late.", false, true));
  check(r.Bool("game.late").Get() && r.CurrentPreset() == Preset::Enhanced, "late setting takes the preset's default");
}

void test_round_trip(const fs::path& dir) {
  const fs::path folder = dir / "round_trip";
  Registry a;
  setup(a);
  a.ApplyPreset(Preset::Enhanced);
  a.SetInt("video.resolution_scale", 3);
  a.SetEnum("gameplay.text_speed", "instant");
  a.SetBool("video.photo_mode", false);
  a.SetFloat("game.walk_speed", 0.1f + 1.2f);
  a.SetInt("audio.cry_volume", 7);
  check(a.Save(folder), "custom settings saved");

  Registry b;
  setup(b);
  sWarnings.clear();
  check(b.Load(folder), "loaded");
  check(sWarnings.empty(), "no warnings reading our own file");
  check(b.CurrentPreset() == Preset::Custom && b.BasePreset() == Preset::Enhanced, "preset and base restored");
  check(all_values_equal(a, b), "every value restored exactly (incl. the float)");
  check(b.Float("game.walk_speed").Get() == 0.1f + 1.2f, "float bit-exact");

  const fs::path named = dir / "named";
  Registry c;
  setup(c);
  c.ApplyPreset(Preset::Enhanced);
  c.SetInt("audio.music_volume", 55);
  c.Save(named);
  const std::string text = read_file(named);
  check(text.find("preset = enhanced\n") != std::string::npos, "file says preset = enhanced");
  check(text.find("\n; widescreen = on\n") != std::string::npos, "preset values written commented out");
  check(text.find("\nmusic_volume = 55\n") != std::string::npos, "preferences written as real values");
  check(text.find("based_on") == std::string::npos, "no based_on for a named preset");

  Registry d;
  setup(d);
  d.Load(named);
  check(d.CurrentPreset() == Preset::Enhanced && all_values_equal(c, d), "named preset round trip");

  std::string edited = text;
  edited.replace(edited.find("; widescreen = on"), 17, "widescreen = off");
  write_file(named, edited);
  Registry e;
  setup(e);
  e.Load(named);
  check(e.CurrentPreset() == Preset::Custom && e.BasePreset() == Preset::Enhanced && !e.Bool("video.widescreen").Get(),
        "hand-edited value: Custom based on Enhanced");
  check(e.Bool("video.photo_mode").Get(), "the rest still Enhanced");

  Registry f;
  setup(f);
  check(!f.Load(dir / "missing") && f.CurrentPreset() == Preset::Original && f.IsOriginal(), "missing file is fine");
}

void test_unknown_kept(const fs::path& dir) {
  const fs::path folder = dir / "unknown";
  write_file(folder, "preset = custom\n"
                     "based_on = original\n"
                     "future_top = 1\n"
                     "[video]\n"
                     "widescreen = on\n"
                     "future_upscaler = fsr 2 ; with spaces\n"
                     "[somemod]\n"
                     "colour.scheme = night\n"
                     "[game]\n"
                     "later = 42\n");
  Registry r;
  setup(r);
  sWarnings.clear();
  r.Load(folder);
  check(sWarnings.empty(), "unknown ids are not warnings");
  check(r.Bool("video.widescreen").Get(), "known value applied");
  r.Save(folder);
  const std::string text = read_file(folder);
  check(text.find("\nfuture_top = 1\n") != std::string::npos, "unknown top-level key written back");
  check(text.find("\nfuture_upscaler = fsr 2 ; with spaces\n") != std::string::npos,
        "unknown key in a known section written back as it was");
  check(text.find("\n[somemod]\ncolour.scheme = night\n") != std::string::npos, "unknown section written back");

  Registry s;
  setup(s);
  s.Load(folder);
  s.Add(Definition::Int("game.later", Category::Gameplay, "Later", "Registered after the load.", 0, 100, 0, 0));
  check(s.Int("game.later").Get() == 42, "late registration picks up the kept value");
  s.Save(folder);
  check(read_file(folder).find("\nlater = 42\n") != std::string::npos, "and it is written as a known setting");
}

void test_bad_values(const fs::path& dir) {
  const fs::path folder = dir / "bad";
  write_file(folder, "\xEF\xBB\xBF"
                     "preset = custom\r\n"
                     "based_on = enhanced\r\n"
                     "[video]\r\n"
                     "widescreen = maybe\r\n"
                     "resolution_scale = 99\r\n"
                     "anti_aliasing = 3x\r\n"
                     "texture_filtering = 2x\r\n"
                     "this line is nonsense\r\n"
                     "[broken\r\n"
                     "ignored = 1\r\n"
                     "[game]\r\n"
                     "walk_speed = nan\r\n"
                     "level_cap = 12abc\r\n"
                     "[gameplay]\r\n"
                     "text_speed =\r\n");
  Registry r;
  setup(r);
  sWarnings.clear();
  check(r.Load(folder), "bad file loaded without a crash");
  for (const std::string& w : sWarnings) {
    std::printf("     warning: %s\n", w.c_str());
  }
  check(sWarnings.size() == 8, "one warning per bad line");
  check(r.CurrentPreset() == Preset::Custom && r.BasePreset() == Preset::Enhanced, "preset read (BOM, CRLF ok)");
  check(r.Bool("video.widescreen").Get() && r.Int("video.resolution_scale").Get() == 0 &&
            r.Enum("video.anti_aliasing").Get() == r.ChoiceIndex("video.anti_aliasing", "4x") &&
            r.Float("game.walk_speed").Get() == 1.0f && r.Int("game.level_cap").Get() == 100 &&
            r.Enum("gameplay.text_speed").Get() == r.ChoiceIndex("gameplay.text_speed", "2x"),
        "bad values fell back to the Enhanced defaults");
  check(r.Enum("video.texture_filtering").Get() == r.ChoiceIndex("video.texture_filtering", "2x"),
        "the good value among them applied");
  r.Save(folder);
  check(read_file(folder).find("ignored") == std::string::npos, "lines under a broken section are dropped");

  write_file(folder, "[gameplay]\nskip_animations = on\n");
  Registry m;
  setup(m);
  sWarnings.clear();
  m.Load(folder);
  check(sWarnings.empty() && m.Bool("gameplay.fast_battle_animations").Get(), "skip_animations read as fast_battle_animations");
  m.Save(folder);
  check(read_file(folder).find("fast_battle_animations = on") != std::string::npos &&
            read_file(folder).find("skip_animations") == std::string::npos,
        "... and written back under the new name");

  write_file(folder, "preset = turbo\n");
  Registry p;
  setup(p);
  sWarnings.clear();
  p.Load(folder);
  check(sWarnings.size() == 1 && p.CurrentPreset() == Preset::Original, "unknown preset: Original, with a warning");
}

void test_text(const fs::path& dir) {
  const auto letters = [](std::string_view t) { return t.find_first_not_of("abc, ") == std::string_view::npos; };
  const auto jump = [&] {
    return Definition::Text("keys.jump", Category::Controls, "Jump", "Keys for jumping.", "a, b", "a, b", letters)
        .Preference();
  };
  Registry r;
  setup(r);
  sWarnings.clear();
  check(r.Add(jump()), "text setting registered");
  check(!r.Add(Definition::Text("keys.bad", Category::Controls, "Bad", "x", "zzz", "zzz", letters)),
        "text default the check refuses: refused");
  check(r.Text("keys.jump") == "a, b" && r.ValueText(r.Find("keys.jump")) == "a, b", "text reads its default");
  check(!r.Int("keys.jump").Valid(), "no number handle for a text");
  check(r.SetText("keys.jump", "c") && r.Text("keys.jump") == "c", "text changed");
  check(!r.SetText("keys.jump", "x") && !r.SetText("keys.jump", "a\nb") && !r.SetText("keys.jump", " a"),
        "refused: the check says no, two lines, leading space");
  check(r.Text("keys.jump") == "c", "refused text left the old one");
  check(r.CurrentPreset() == Preset::Original && r.IsOriginal(), "a text preference keeps Original");

  const fs::path folder = dir / "text";
  r.SetText("keys.jump", "b, c");
  r.Save(folder);
  check(read_file(folder).find("\n[keys]\n; Jump (does nothing yet): Keys for jumping.\njump = b, c\n") != std::string::npos,
        "written in its own section, with its description");
  Registry b;
  setup(b);
  b.Add(jump());
  b.Load(folder);
  check(b.Text("keys.jump") == "b, c", "text round trip");

  write_file(folder, "[keys]\njump = q\nspelt_wrong = a\n");
  Registry c;
  setup(c);
  c.Add(jump());
  sWarnings.clear();
  c.Load(folder);
  check(c.Text("keys.jump") == "a, b" && sWarnings.size() == 1, "invalid text: the default, with a warning");
  const std::vector<std::string> unknown = c.UnknownIds();
  check(unknown.size() == 1 && unknown[0] == "keys.spelt_wrong", "unknown ids listed");
}

void test_rules() {
  Registry r;
  setup(r);
  const std::vector<std::string> broken = CheckRules(r);
  for (const std::string& b : broken) {
    std::printf("     broken: %s\n", b.c_str());
  }
  check(broken.empty(), "the standard list keeps every rule");

  r.ApplyPreset(Preset::Enhanced);
  check(!r.IsOriginal() && !r.NotOriginal().empty(), "Enhanced changes something");
  r.ApplyPreset(Preset::Original);
  check(r.IsOriginal() && r.NotOriginal().empty(), "Original again: IsOriginal");
  bool boolsOff = true, enumsFirst = true;
  for (size_t i = 0; i < r.Count(); i++) {
    const Definition& d = r.Get(i);
    if (d.preference) {
      continue;
    }
    const bool keeps = d.id.find(".original_") != std::string::npos || d.id.find(".keep_") != std::string::npos;
    if (d.type == Type::Bool) {
      boolsOff &= r.ValueOf(i).i == (keeps ? 1 : 0);
    }
    if (d.type == Type::Enum) {
      enumsFirst &= r.ValueOf(i).i == 0;
    }
  }
  check(boolsOff, "Original: every on/off enhancement is off (original_... ones on)");
  check(enumsFirst, "Original: every choice enhancement at its first (GameCube) choice");
  check(r.Enum("system.disc_timing").Get() == r.ChoiceIndex("system.disc_timing", "gamecube") &&
            r.Enum("gameplay.text_speed").Get() == r.ChoiceIndex("gameplay.text_speed", "original") &&
            r.Int("video.resolution_scale").Get() == 1,
        "Original: GameCube disc timing, original text speed, native resolution");

  Registry bad;
  bad.SetWarningSink([](const std::string&) {});
  bad.Add(Definition::Bool("x.cheat", Category::Gameplay, "Cheat", "On in Original.", true, true));
  bad.Add(Definition::Bool("x.original_look", Category::Video, "Look", "Off in Original.", false, true));
  bad.Add(Definition::Enum("x.mode", Category::Gameplay, "Mode", "First choice isn't the GameCube's.",
                           {{"fast", "Fast"}, {"original", "Original"}}, "original", "fast"));
  bad.Add(Definition::Enum("x.mode2", Category::Gameplay, "Mode", "Original isn't the first choice.",
                           {{"original", "Original"}, {"fast", "Fast"}}, "fast", "fast"));
  bad.Add(Definition::Text("x.words", Category::Gameplay, "Words", "A text enhancement.", "a", "b"));
  bad.Add(Definition::Int("x.nodesc", Category::Gameplay, "", "no full stop", 0, 1, 0, 0));
  check(CheckRules(bad).size() == 7, "7 broken rules found in 6 bad declarations");
}

void test_overrides(const fs::path& dir) {
  Registry r;
  setup(r);
  const EnumSetting disc = r.Enum("system.disc_timing");
  check(r.Summary() == "original (preset original)", "summary: original");

  check(r.Override("system.disc_timing", "fast"), "override disc timing");
  check(disc.Get() == r.ChoiceIndex("system.disc_timing", "fast") && r.IsOverridden(disc.Index()), "game sees fast");
  check(!r.IsOriginal() && r.CurrentPreset() == Preset::Original, "not Original now, but the preset is unchanged");
  check(r.Summary() == "not original (preset original): system.disc_timing=fast; for this run: system.disc_timing=fast",
        "summary names it, as run-only");
  const fs::path folder = dir / "overrides";
  r.Save(folder);
  check(read_file(folder).find("\n; disc_timing = gamecube\n") != std::string::npos, "the override is not saved");
  check(!r.Override("system.disc_timing", "warp") && !r.Override("no.such", "1"), "bad overrides refused");

  const FloatSetting speed = r.Float("system.game_speed");
  r.ClearOverrides();
  check(r.IsOriginal() && !r.IsOverridden(disc.Index()), "overrides cleared");
  check(!r.SetFloat("system.game_speed", 4.0f) && speed.Get() == 1.0f, "a testing setting can't be set for good");
  check(r.Override("system.game_speed", "4") && speed.Get() == 4.0f, "... only overridden");
  check(r.IsOriginal() && r.Summary() == "original (preset original); for this run: system.game_speed=4",
        "game speed keeps Original, but is listed");
  r.Save(folder);
  check(read_file(folder).find("game_speed") == std::string::npos, "testing settings never written");
  write_file(folder, "[system]\ngame_speed = 8\n");
  Registry t;
  setup(t);
  sWarnings.clear();
  t.Load(folder);
  check(t.Float("system.game_speed").Get() == 1.0f && sWarnings.size() == 1, "game_speed in the file: ignored, warned");

  Registry e;
  setup(e);
  e.ApplyPreset(Preset::Enhanced);
  e.SetInt("audio.music_volume", 30);
  e.OverridePreset(Preset::Original);
  check(e.IsOriginal() && !e.Bool("video.widescreen").Get(), "XD_PRESET=original: the game sees Original");
  check(e.CurrentPreset() == Preset::Enhanced && e.SavedValueOf(e.Find("video.widescreen")).i == 1,
        "... while the player's Enhanced is kept");
  check(e.Int("audio.music_volume").Get() == 30, "... and preferences stay the player's");
  const std::string summary = e.Summary();
  check(summary.rfind("original (preset enhanced); for this run: ", 0) == 0 &&
            summary.find("video.widescreen=off") != std::string::npos &&
            summary.find("system.disc_timing=gamecube") != std::string::npos,
        "summary lists what the run-only preset changed");
  sWarnings.clear();
  check(e.ApplyOverrides(" video.widescreen = on ; gameplay.text_speed=instant;bogus;no.such=1;preset=turbo; ") == 3,
        "override list: 3 of 5 refused");
  check(e.Bool("video.widescreen").Get() && e.Enum("gameplay.text_speed").Get() == 3 && sWarnings.size() == 3,
        "the good ones applied, each refusal warned");
  e.ClearOverrides();
  check(e.Bool("video.widescreen").Get() && e.CurrentPreset() == Preset::Enhanced, "cleared: the player's values");

  e.SetEnum("gameplay.text_speed", "instant");
  e.Override("system.game_speed", "8");
  const std::string replay = e.ReplayText();
  check(replay.rfind("preset=original;", 0) == 0 && replay.find("gameplay.text_speed=instant") != std::string::npos &&
            replay.find("game_speed") == std::string::npos && replay.find("music_volume") == std::string::npos,
        "replay text: the game settings only");
  Registry p;
  setup(p);
  p.SetBool("gameplay.fix_original_bugs", true);
  check(p.ApplyOverrides(replay) == 0, "replay text applies without refusals");
  bool same = true;
  for (size_t i = 0; i < e.Count(); i++) {
    if (!e.Get(i).preference) {
      same &= e.ValueText(i) == p.ValueText(p.Find(e.Get(i).id));
    }
  }
  check(same, "every game setting the same as in the recorded run");
  Registry o;
  setup(o);
  check(o.ReplayText() == "preset=original", "an Original run: just the preset");
}

void test_format(const fs::path& dir) {
  const fs::path old = dir / "format0";
  write_file(old, "preset = custom\nbased_on = enhanced\n[video]\nwidescreen = off\n");
  Registry a;
  setup(a);
  sWarnings.clear();
  a.Load(old);
  check(sWarnings.empty() && a.LastLoad().found && a.LastLoad().format == 0 && a.LastLoad().migrated,
        "format 0: migrated without warnings");
  check(a.CurrentPreset() == Preset::Custom && !a.Bool("video.widescreen").Get() &&
            a.Bool("video.photo_mode").Get(),
        "format 0: values read");
  a.Save(old);
  check(read_file(old).find("\nformat = 1\npreset = custom\n") != std::string::npos, "written as format 1");
  Registry a2;
  setup(a2);
  a2.Load(old);
  check(a2.LastLoad().format == 1 && !a2.LastLoad().migrated && all_values_equal(a, a2), "format 1 round trip");

  const fs::path newer = dir / "format9";
  write_file(newer, "format = 9\npreset = enhanced\n[video]\nhdr = on\n");
  Registry b;
  setup(b);
  sWarnings.clear();
  b.Load(newer);
  check(b.LastLoad().newerFormat && sWarnings.size() == 1 && b.CurrentPreset() == Preset::Enhanced,
        "newer format: read, one warning");
  check(b.LastLoad().unknownIds.size() == 1 && b.LastLoad().unknownIds[0] == "video.hdr", "its unknown key reported");
  check(b.Save(newer) && fs::exists(newer / "settings.ini.format9.bak") &&
            read_file(newer).find("\nhdr = on\n") != std::string::npos,
        "saved: backup kept, unknown key written back");

  const auto renamed = [] {
    return Definition::Bool("game.wide_ui", Category::Video, "Wide menus", "Test rename.", false, true)
        .FormerIds({"game.wide_menus"});
  };
  const fs::path ren = dir / "renamed";
  write_file(ren, "preset = custom\n[game]\nwide_menus = on\n");
  Registry c;
  setup(c);
  c.Add(renamed());
  c.Load(ren);
  check(c.Bool("game.wide_ui").Get() && c.LastLoad().renamed.size() == 1 && c.UnknownIds().empty(),
        "former id read into the renamed setting");
  c.Save(ren);
  const std::string text = read_file(ren);
  check(text.find("\nwide_ui = on\n") != std::string::npos && text.find("wide_menus =") == std::string::npos,
        "written back under the new id");
  write_file(ren, "preset = custom\n[game]\nwide_menus = on\n");
  Registry d;
  setup(d);
  d.Load(ren);
  d.Add(renamed());
  check(d.Bool("game.wide_ui").Get() && d.UnknownIds().empty(), "former id also found by a late registration");
  check(!d.Add(Definition::Bool("game.other", Category::Video, "x", "x.", false, false).FormerIds({"game.wide_ui"})),
        "a former id that is taken is refused");
}

void test_threads() {
  Registry r;
  setup(r);
  const BoolSetting widescreen = r.Bool("video.widescreen");
  const IntSetting scale = r.Int("video.resolution_scale");
  std::atomic<bool> stop{false};
  std::atomic<int> bad{0};
  std::atomic<long> reads{0};
  std::thread reader([&] {
    while (!stop.load()) {
      const int32_t s = scale.Get();
      bad += (s != 0 && s != 1) ? 1 : 0;
      (void)widescreen.Get();
      (void)r.IsOriginal();
      reads++;
    }
  });
  while (reads.load() == 0) {
    std::this_thread::yield();
  }
  for (int n = 0; n < 20000; n++) {
    r.ApplyPreset(n & 1 ? Preset::Enhanced : Preset::Original);
  }
  stop = true;
  reader.join();
  if (bad.load() != 0) {
    std::printf("     %d torn reads in %ld\n", bad.load(), reads.load());
  }
  check(bad.load() == 0, "reads from another thread only ever see whole values");
}

void test_c_api() {
  Registry& g = Global();
  RegisterStandardSettings(g);
  const int32_t* widescreen = EngineSettings_IntSlot("video.widescreen");
  check(widescreen && *widescreen == 0, "C: widescreen slot reads off");
  check(EngineSettings_IsOriginal() == 1, "C: Original");
  g.ApplyPreset(Preset::Enhanced);
  check(*widescreen == 1, "C: slot follows the preset");
  check(EngineSettings_IsOriginal() == 0, "C: Enhanced is not Original");
  check(!EngineSettings_IntSlot("no.such") && !EngineSettings_FloatSlot("video.widescreen"), "C: bad lookups NULL");
  check(EngineSettings_ChoiceIndex("system.disc_timing", "fast") == 1 &&
            EngineSettings_ChoiceIndex("system.disc_timing", "warp") == -1,
        "C: choice index");
  const int32_t* disc = EngineSettings_IntSlot("system.disc_timing");
  check(disc && *disc == EngineSettings_ChoiceIndex("system.disc_timing", "fast"), "C: Enhanced loads fast");
  const float* speed = EngineSettings_FloatSlot("system.game_speed");
  g.Override("system.game_speed", "2.5");
  check(speed && *speed == 2.5f, "C: float slot sees the override");
  char small[9];
  const size_t full = EngineSettings_Summary(small, sizeof small);
  check(full == g.Summary().size() && std::string(small) == "not orig", "C: summary cut like snprintf");
}

}

int main(int argc, char** argv) {
  const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "engine_settings_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  test_presets();
  test_registration();
  test_round_trip(dir);
  test_unknown_kept(dir);
  test_bad_values(dir);
  test_text(dir);
  test_rules();
  test_overrides(dir);
  test_format(dir);
  test_threads();
  test_c_api();

  fs::remove_all(dir, ec);
  std::printf("%s\n", sOk ? "all passed" : "FAILED");
  return sOk ? 0 : 1;
}
