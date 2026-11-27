#include <xd_saveedit_write.h>

#include <gc_memcard.h>
#include <i18n.h>
#include <playthrough.h>
#include <save_backups.h>
#include <saveext.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

namespace xd::saveedit {

namespace fs = std::filesystem;
namespace xs = xd::save;
namespace ep = engine::playthrough;
namespace mc = engine::memcard;
using engine::i18n::Tr;

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

bool ReadBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool WriteBytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

fs::path SetupFolderOf(const CommitRequest& r) { return r.setupFolder.empty() ? r.cardRoot / L"playthroughs" / L"Card A" : r.setupFolder; }

}

std::string MarkLine(const std::string& when, const std::string& reasons) {
  return when + ": save editor" + (reasons.empty() ? "" : " (" + reasons + ")");
}

std::string WhyNotNow(const CommitRequest& r) {
  if (r.cardIsFree && !r.cardIsFree()) return Tr("The game is running and has the memory card open. Close it first.");
  std::error_code ec;
  if (!fs::is_regular_file(r.gciFile, ec)) return Tr("The save's card file is not there any more.");
  return "";
}

CommitResult Commit(const Editor& editor, const Tables& tables, const CommitRequest& req) {
  CommitResult res;
  const auto fail = [&](const std::string& why) {
    res.ok = false;
    res.message = Fmt(Tr("Nothing was written: %s"), why.c_str());
    return res;
  };
  if (!editor.Dirty()) return fail(Tr("nothing was changed."));
  if (const std::string why = WhyNotNow(req); !why.empty()) return fail(why);

  const std::vector<std::string> problems = editor.Problems(tables);
  if (!problems.empty()) {
    std::string text = Tr("the changes would not work in the game:");
    for (size_t i = 0; i < problems.size() && i < 4; i++) text += " " + problems[i] + ";";
    return fail(text);
  }
  std::string error;
  const std::vector<uint8_t> newData = editor.Build(error);
  if (newData.empty()) return fail(error);
  std::vector<uint8_t> gciBytes;
  mc::CardFile card;
  if (!ReadBytes(req.gciFile, gciBytes) || !mc::ReadGci(gciBytes, card, error)) return fail(Tr("the save's card file can't be read."));
  if (card.data.size() < xs::kFileSize || std::memcmp(card.data.data(), editor.original().data(), xs::kFileSize) != 0) {
    return fail(Tr("the save changed since it was opened here (the game, a restore or another program wrote to it). Open it again."));
  }

  const std::u16string oldName = editor.OriginalTrainerName();
  const xs::Save& now = editor.save();
  res.oldKey = PlaythroughKey(now.trainerOtId, oldName);
  res.newKey = PlaythroughKey(now.trainerOtId, now.trainerName);
  const fs::path folder = SetupFolderOf(req);
  const bool renamed = res.oldKey != res.newKey;
  std::error_code ec;
  if (renamed) {
    for (const char* ext : {".json", ".ext", ".png"}) {
      if (fs::exists(folder / (res.newKey + ext), ec)) {
        return fail(Tr("another playthrough already has files for a trainer with that name and ID. Pick another name."));
      }
    }
  }

  std::vector<mc::backups::Companion> companions;
  std::vector<uint8_t> extOld;
  const bool haveExt = ReadBytes(engine::saveext::FileFor(folder, res.oldKey), extOld);
  if (haveExt) companions.push_back({res.oldKey + ".ext", extOld});
  std::string backupError;
  res.backupFolder = mc::backups::AddWith(mc::backups::FolderFor(req.cardRoot), "Card A", req.gciFile.filename().string(), gciBytes, companions,
                                          std::time(nullptr), mc::backups::kKeep, &backupError);
  if (res.backupFolder.empty() && !backupError.empty()) return fail(Fmt(Tr("the backup of the save could not be made (%s)."), backupError.c_str()));

  mc::CardFile out = card;
  std::memcpy(out.data.data(), newData.data(), xs::kFileSize);
  const std::vector<uint8_t> newGci = mc::MakeGci(out);
  fs::path tmp = req.gciFile;
  tmp += ".edit.tmp";
  if (!WriteBytes(tmp, newGci)) {
    fs::remove(tmp, ec);
    return fail(Tr("the new save file could not be written (is the disk full?)."));
  }
  {
    std::vector<uint8_t> back;
    mc::CardFile backCard;
    xs::Save check;
    xs::ReadReport rep;
    std::string err;
    if (!ReadBytes(tmp, back) || back != newGci || !mc::ReadGci(back, backCard, err) || backCard.data.size() < xs::kFileSize ||
        !xs::ReadSave(std::vector<uint8_t>(backCard.data.begin(), backCard.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize)), check, rep, err)) {
      fs::remove(tmp, ec);
      return fail(Tr("the new save file did not read back correctly."));
    }
  }
  if (req.cardIsFree && !req.cardIsFree()) {
    fs::remove(tmp, ec);
    return fail(Tr("the game was started while the changes were prepared. Close it and try again."));
  }

  ep::Setup setup;
  const fs::path oldSetup = ep::FileFor(folder, res.oldKey);
  bool haveSetup = fs::exists(oldSetup, ec);
  std::string setupError;
  if (haveSetup && !ep::ReadFile(oldSetup, setup, setupError)) {
    fs::remove(tmp, ec);
    return fail(Fmt(Tr("the playthrough's setup file can't be read (%s), so the mark could not be kept."), setupError.c_str()));
  }
  if (!haveSetup) {
    setup.chosenBy = "editor";
    setup.started = ep::Now();
    setup.trainerId = now.TrainerId();
    setup.secretId = now.SecretId();
  }
  if (editor.ExtraShadowsChanged()) {
    const bool has = !editor.ExtraShadowSection().empty();
    setup.extRequired.erase(std::remove(setup.extRequired.begin(), setup.extRequired.end(), std::string("shadow")), setup.extRequired.end());
    if (has) setup.extRequired.push_back("shadow");
  }
  if (editor.ShadowOrdersChanged()) setup.editorShadows = editor.ShadowOrders();
  setup.trainer = xs::ToUtf8(now.trainerName);
  setup.trainerId = now.TrainerId();
  setup.secretId = now.SecretId();
  const bool mark = editor.WillMark();
  if (mark) {
    setup.cheatsUsed.push_back(MarkLine(ep::Now(), editor.MarkReason()));
    res.marked = true;
  }
  std::error_code mk;
  fs::create_directories(folder, mk);
  const fs::path newSetup = ep::FileFor(folder, res.newKey);
  if (mark || renamed || !haveSetup) {
    if (!ep::WriteFile(newSetup, setup)) {
      fs::remove(tmp, ec);
      return fail(Tr("the playthrough's setup file could not be written."));
    }
    if (renamed && haveSetup) {
      if (mark) {
        ep::Setup keep;
        std::string e2;
        if (ep::ReadFile(oldSetup, keep, e2)) {
          keep.cheatsUsed = setup.cheatsUsed;
          ep::WriteFile(oldSetup, keep);
        }
      }
    }
  }

  bool staged = false;
  const bool shadowChanged = editor.ExtraShadowsChanged();
  if (haveExt || shadowChanged) {
    engine::saveext::File file;
    bool have = false;
    if (haveExt) {
      const engine::saveext::Loaded loaded = engine::saveext::Load(folder, res.oldKey, engine::saveext::Hash64(editor.original().data(), xs::kFileSize));
      if (loaded.state == engine::saveext::State::Paired) {
        file = loaded.file;
        have = true;
      }
    }
    if (have || shadowChanged) {
      if (shadowChanged) {
        const std::vector<uint8_t> section = editor.ExtraShadowSection();
        file.sections.erase(std::remove_if(file.sections.begin(), file.sections.end(), [](const engine::saveext::Section& x) { return x.name == "shadow"; }), file.sections.end());
        if (!section.empty()) {
          engine::saveext::Section sec;
          sec.name = "shadow";
          sec.version = 1;
          sec.required = true;
          sec.data = section;
          file.sections.push_back(std::move(sec));
        }
      }
      file.saveHash = engine::saveext::Hash64(newData.data(), xs::kFileSize);
      file.writer = req.writer;
      staged = engine::saveext::Stage(folder, res.newKey, file);
      if (!staged) {
        fs::remove(tmp, ec);
        return fail(Tr("the save extension could not be made again for the new save."));
      }
    }
  }
  fs::rename(tmp, req.gciFile, ec);
  if (ec) {
    fs::remove(tmp, ec);
    if (staged) engine::saveext::DropStaged(folder, res.newKey);
    return fail(Fmt(Tr("the save file could not be replaced (%s)."), ec.message().c_str()));
  }
  if (staged) {
    engine::saveext::Commit(folder, res.newKey);
    res.extensionRepaired = true;
  }

  if (renamed) {
    for (const char* ext : {".png"}) {
      fs::rename(folder / (res.oldKey + ext), folder / (res.newKey + ext), ec);
    }
    if (haveExt && !staged) {
      fs::rename(engine::saveext::FileFor(folder, res.oldKey), engine::saveext::FileFor(folder, res.newKey), ec);
    } else if (staged) {
      fs::remove(engine::saveext::FileFor(folder, res.oldKey), ec);
    }
  }

  res.ok = true;
  {
    const int n = static_cast<int>(editor.changes().size());
    std::string text = Fmt(n == 1 ? Tr("Saved %d change") : Tr("Saved %d changes"), n);
    text += res.marked ? Tr("; the playthrough is marked as \"cheats used\"") : Tr("; the playthrough is not marked");
    text += res.backupFolder.empty() ? std::string(".") : Fmt(Tr(". The save as it was is in the backups (%s)."), res.backupFolder.filename().string().c_str());
    res.message = text;
  }
  return res;
}

}
