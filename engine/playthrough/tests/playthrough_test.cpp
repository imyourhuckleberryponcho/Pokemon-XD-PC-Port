#include <playthrough.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace ep = engine::playthrough;
namespace em = engine::mods;

namespace {

bool sOk = true;

void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

em::ModFolder Found(const std::string& id, const std::string& version) {
  em::ModFolder f;
  f.manifest.id = id;
  f.manifest.name = "Mod " + id;
  f.manifest.version = version;
  return f;
}

}

int main(int argc, char** argv) {
  const fs::path root = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path() / "xd_playthrough_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);

  ep::Setup vanilla;
  check(ep::Label(vanilla) == "Faithful \xC2\xB7 Vanilla", "label: Faithful · Vanilla");
  ep::Setup modded;
  modded.engine = ep::Engine::Enhanced;
  modded.mods = {{"harder", "Harder trainers", "1.2", em::Applies::NewPlaythrough},
                 {"music", "New music", "2.0", em::Applies::AnyTime}};
  check(ep::Label(modded) == "Enhanced \xC2\xB7 Mods: harder 1.2", "label: only world mods are named");
  ep::Setup looks = vanilla;
  looks.mods = {{"music", "New music", "2.0", em::Applies::AnyTime}};
  check(ep::Label(looks) == "Faithful \xC2\xB7 Vanilla", "label: a looks-and-sound mod keeps a playthrough Vanilla");
  ep::Setup changed = modded;
  changed.setupChanged = {"2026-11-09 20:00: harder 1.2 missing"};
  check(ep::Label(changed).find("setup changed") != std::string::npos, "label: says \"setup changed\"");

  modded.started = "2026-11-08 14:03";
  modded.portVersion = "0.3.0";
  modded.chosenBy = "screen";
  modded.trainer = "Mich\xC3\xA9l \"Q\"";
  modded.trainerId = 12345;
  modded.secretId = 54321;
  modded.setupChanged = {"a \\ b"};
  const fs::path file = ep::FileFor(root / "playthroughs" / "Card A", "pt_0000abcd/x y");
  check(file.filename() == "pt_0000abcd_x_y.json", "file name: only safe letters");
  check(ep::WriteFile(file, modded), "write the file (folders made)");
  ep::Setup back;
  std::string error;
  check(ep::ReadFile(file, back, error), "read it back");
  check(back.engine == ep::Engine::Enhanced && back.mods.size() == 2 && back.mods[0].id == "harder" &&
            back.mods[0].version == "1.2" && back.mods[1].applies == em::Applies::AnyTime &&
            back.started == modded.started && back.portVersion == "0.3.0" && back.chosenBy == "screen" && back.trainer == modded.trainer &&
            back.trainerId == 12345 && back.secretId == 54321 && back.setupChanged == modded.setupChanged,
        "round trip: every field (quotes, accents and backslashes too)");
  check(ep::ToJson(back) == ep::ToJson(modded), "round trip: the same text again");
  {
    ep::Setup run = modded;
    check(!run.Failed() && ep::ToJson(run).find("challenge_log") == std::string::npos, "no challenge log: not failed, not written");
    run.challengeLog = {"2026-11-09 18:40: lost: species 135 fainted (Nuzlocke), moved to the PC"};
    check(!run.Failed() && ep::Label(run).find("run failed") == std::string::npos, "a lost Pokemon is not a failed run");
    run.challengeLog.push_back("2026-11-09 18:55: failed: every party Pokemon fainted (Nuzlocke)");
    check(run.Failed() && ep::Label(run).find("run failed") != std::string::npos, "label: says \"run failed\"");
    ep::Setup again;
    check(ep::FromJson(ep::ToJson(run), again, error) && again.challengeLog == run.challengeLog && again.Failed(),
          "challenge log: round trip");
    check(ep::ToJson(run).find("challenge_state") == std::string::npos, "no challenge state: not written");
    run.challengeState = {"lost 135 1a2b3c4d 0001e240", "area 15400 \"Cipher Lab\""};
    check(ep::FromJson(ep::ToJson(run), again, error) && again.challengeState == run.challengeState &&
              again.challengeLog == run.challengeLog,
          "challenge state: round trip");
    run.challengeState.clear();
    check(ep::FromJson(ep::ToJson(run), again, error) && again.challengeState.empty(), "challenge state: emptied again");
    check(ep::ToJson(run).find("battles_won") == std::string::npos && !again.battlesWonKept, "battles won not kept: not written");
    run.battlesWonKept = true;
    check(ep::ToJson(run).find("\"battles_won\": []") != std::string::npos, "battles won kept, none yet: an empty list is written");
    check(ep::FromJson(ep::ToJson(run), again, error) && again.battlesWonKept && again.battlesWon.empty(), "battles won: empty round trip");
    run.battlesWon = {216, 375, 393};
    check(ep::FromJson(ep::ToJson(run), again, error) && again.battlesWonKept && again.battlesWon == run.battlesWon,
          "battles won: round trip");
  }
  {
    ep::Setup old;
    check(ep::FromJson("{\"format\": 1, \"engine\": \"faithful\"}", old, error) && old.chosenBy.empty() &&
              ep::ToJson(old).find("chosen_by") == std::string::npos,
          "an older file (no chosen_by): read, and not written back with one");
  }
  {
    ep::Setup quiet;
    quiet.chosenBy = "run";
    quiet.started = "2026-11-09 10:00";
    check(ep::EngineQuestionDue(quiet, ep::Engine::Enhanced), "question: a Faithful save + Enhanced settings: asked");
    check(!ep::EngineQuestionDue(quiet, ep::Engine::Faithful), "question: not when both are Faithful");
    ep::Setup other = quiet;
    other.chosenBy = "screen";
    check(ep::EngineQuestionDue(other, ep::Engine::Enhanced), "question: also for a setup chosen on the screen");
    other.chosenBy.clear();
    check(ep::EngineQuestionDue(other, ep::Engine::Enhanced), "question: also for an older file (no chosen_by)");
    other.engine = ep::Engine::Enhanced;
    check(ep::EngineQuestionDue(other, ep::Engine::Faithful) && !ep::EngineQuestionDue(other, ep::Engine::Enhanced),
          "question: an Enhanced save + Faithful settings: asked; both Enhanced: not");
    check(quiet.PureFaithful() && quiet.StartedAs() == ep::Engine::Faithful && !other.PureFaithful() &&
              other.StartedAs() == ep::Engine::Enhanced,
          "pure Faithful: a Faithful save never switched; an Enhanced one is not");
    check(ep::ToJson(quiet).find("engine_changed") == std::string::npos && ep::ToJson(quiet).find("engine_started") == std::string::npos,
          "never switched: no engine_changed / engine_started in the file");
    const std::string before = ep::ToJson(quiet);

    ep::Setup switched = quiet;
    ep::SwitchEngine(switched, ep::Engine::Enhanced, "2026-11-09 20:15");
    check(switched.engine == ep::Engine::Enhanced && switched.chosenBy == "run" && switched.StartedAs() == ep::Engine::Faithful &&
              switched.engineChanged == std::vector<std::string>{"2026-11-09 20:15: switched from Faithful to Enhanced at Continue"},
          "switch forward: Enhanced, started as Faithful, dated note (chosen_by untouched)");
    check(ep::Label(switched) == "Enhanced \xC2\xB7 Vanilla \xC2\xB7 was Faithful", "switch forward: label ends \"was Faithful\"");
    check(!switched.PureFaithful(), "switch forward: not pure Faithful");
    check(!ep::EngineQuestionDue(switched, ep::Engine::Enhanced) && ep::EngineQuestionDue(switched, ep::Engine::Faithful),
          "switch forward: no question with Enhanced settings; asked again with Faithful ones");
    ep::Setup again;
    const fs::path qfile = ep::FileFor(root / "playthroughs" / "Card A", "pt_question");
    check(ep::WriteFile(qfile, switched) && ep::ReadFile(qfile, again, error) && again.engine == ep::Engine::Enhanced &&
              again.chosenBy == "run" && again.engineChanged == switched.engineChanged && again.started == quiet.started &&
              again.StartedAs() == ep::Engine::Faithful && ep::Label(again) == ep::Label(switched) && ep::ToJson(again) == ep::ToJson(switched),
          "switch forward: file round trip");
    check(ep::ToJson(switched).find("\"engine_started\": \"faithful\"") != std::string::npos, "switch forward: the file says what it began with");
    ep::SwitchEngine(switched, ep::Engine::Enhanced, "2026-11-10 08:00");
    check(switched.engineChanged.size() == 1, "switching to the engine it has adds no note");

    ep::SwitchEngine(switched, ep::Engine::Faithful, "2026-11-12 18:02");
    check(switched.engine == ep::Engine::Faithful && switched.engineChanged.size() == 2 &&
              switched.engineChanged[1] == "2026-11-12 18:02: switched from Enhanced to Faithful at Continue" &&
              switched.StartedAs() == ep::Engine::Faithful,
          "switch back: Faithful, both notes kept, started as Faithful");
    check(ep::Label(switched) == "Faithful \xC2\xB7 Vanilla \xC2\xB7 was Enhanced", "switch back: label ends \"was Enhanced\"");
    check(!switched.PureFaithful(), "switch back: Faithful now, but not pure Faithful");
    check(ep::EngineQuestionDue(switched, ep::Engine::Enhanced) && !ep::EngineQuestionDue(switched, ep::Engine::Faithful),
          "switch back: no question with Faithful settings");
    check(ep::FromJson(ep::ToJson(switched), again, error) && again.engineChanged == switched.engineChanged && !again.PureFaithful() &&
              again.StartedAs() == ep::Engine::Faithful && ep::Label(again) == ep::Label(switched) && ep::ToJson(again) == ep::ToJson(switched),
          "switch back: round trip (history kept)");
    switched.setupChanged = {"2026-11-13 09:00: harder 1.2 missing"};
    switched.mods = {{"harder", "Harder trainers", "1.2", em::Applies::NewPlaythrough}};
    check(ep::Label(switched) == "Faithful \xC2\xB7 Mods: harder 1.2 \xC2\xB7 was Enhanced \xC2\xB7 setup changed",
          "label with mods, the marker and \"setup changed\"");

    ep::Setup began;
    began.engine = ep::Engine::Enhanced;
    ep::SwitchEngine(began, ep::Engine::Faithful, "2026-11-12 18:02");
    check(ep::Label(began) == "Faithful \xC2\xB7 Vanilla \xC2\xB7 was Enhanced" && !began.PureFaithful() &&
              began.StartedAs() == ep::Engine::Enhanced && ep::FromJson(ep::ToJson(began), again, error) &&
              again.StartedAs() == ep::Engine::Enhanced,
          "begun Enhanced, switched to Faithful: marker, not pure, started as Enhanced (round trip)");
    std::string text = ep::ToJson(began);
    const size_t at = text.find("  \"engine_started\"");
    text.erase(at, text.find('\n', at) + 1 - at);
    check(ep::FromJson(text, again, error) && again.StartedAs() == ep::Engine::Enhanced, "engine_started missing: worked out");
    check(ep::ToJson(quiet) == before, "keeping: the setup is untouched");
  }
  check(!ep::FromJson("{\"format\": 1, \"engine\": \"turbo\"}", back, error), "a bad engine is refused");
  check(!ep::FromJson("{\"format\": 9, \"engine\": \"faithful\"}", back, error), "a newer format is refused");
  check(!ep::FromJson("not json", back, error), "not JSON is refused");
  check(!fs::exists(fs::path(file).concat(".tmp")), "no temporary file left");

  ep::Check c = ep::Compare(modded, {Found("harder", "1.2"), Found("music", "2.0")});
  check(c.Ok(), "check: everything there");
  c = ep::Compare(modded, {Found("music", "2.0")});
  check(c.worldProblems.size() == 1 && c.worldProblems[0].find("not in the mods folder") != std::string::npos &&
            c.notes.empty(),
        "check: a missing world mod is a problem");
  c = ep::Compare(modded, {Found("HARDER", "1.3"), Found("music", "2.0")});
  check(c.worldProblems.size() == 1 && c.worldProblems[0].find("now version 1.3") != std::string::npos,
        "check: a world mod in another version is a problem (ids ignore case)");
  c = ep::Compare(modded, {Found("harder", "1.2")});
  check(c.worldProblems.empty() && c.notes.size() == 1, "check: a missing looks-and-sound mod is only a note");
  c = ep::Compare(modded, {Found("harder", "1.2.0"), Found("music", "9")});
  check(c.Ok(), "check: 1.2.0 = 1.2; another version of a looks-and-sound mod is fine");
  check(ep::WorldModIds(modded) == std::vector<std::string>{"harder"}, "world mod ids");

  {
    engine::settings::Registry reg;
    reg.SetWarningSink([](const std::string&) {});
    engine::settings::RegisterStandardSettings(reg);
    check(ep::DefaultEngine(reg) == ep::Engine::Faithful, "engine: the Original preset defaults to Faithful");
    reg.ApplyPreset(engine::settings::Preset::Enhanced);
    check(ep::DefaultEngine(reg) == ep::Engine::Enhanced, "engine: the Enhanced preset defaults to Enhanced");
    reg.Override("system.card_timing", "gamecube");
    const std::string changed = ep::ApplyEngine(reg, ep::Engine::Faithful);
    check(reg.Text("gameplay.text_speed").empty() && reg.ValueText(reg.Find("gameplay.text_speed")) == "original" &&
              reg.ValueText(reg.Find("system.disc_timing")) == "gamecube",
          "engine: Faithful holds text speed and disc timing at the GameCube's");
    check(reg.Bool("video.widescreen").Get(), "engine: Faithful leaves the picture alone (widescreen stays on)");
    check(changed.find("gameplay.text_speed=original") != std::string::npos &&
              changed.find("system.card_timing") == std::string::npos,
          "engine: what changed is listed (not the setting the environment holds)");
    check(reg.TextOf(reg.Find("gameplay.text_speed"), reg.SavedValueOf(reg.Find("gameplay.text_speed"))) == "2x",
          "engine: the player's own value is kept (for settings.ini)");
    check(ep::DefaultEngine(reg) == ep::Engine::Enhanced, "engine: held Faithful, the player's own settings still say Enhanced");
    const std::string back2 = ep::ApplyEngine(reg, ep::Engine::Enhanced);
    check(reg.ValueText(reg.Find("gameplay.text_speed")) == "2x" && reg.ValueText(reg.Find("system.card_timing")) == "gamecube",
          "engine: Enhanced gives the player's values back; the environment's override stays");
    check(back2.find("gameplay.text_speed=2x") != std::string::npos && back2.find("system.disc_timing=fast") != std::string::npos &&
              back2.find("video.widescreen") == std::string::npos && back2.find("audio.") == std::string::npos,
          "engine: the switch back lists what changed for the game (no picture setting)");
    const std::vector<std::string> names = ep::EngineSettingNames(reg);
    const auto has = [&](const char* n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    check(has("Text speed") && has("Fast-forward key") && has("Fast battle animations") && has("Loading speed") &&
              has("Memory card speed") && !has("Widescreen") && !has("Resolution") && !has("Volume") && !has("Fast-forward speed"),
          "engine setting names: the speed-ups and timings, no picture / sound / preference");
  }

  fs::remove_all(root, ec);
  std::printf(sOk ? "playthrough_test: all passed\n" : "playthrough_test: FAILED\n");
  return sOk ? 0 : 1;
}
