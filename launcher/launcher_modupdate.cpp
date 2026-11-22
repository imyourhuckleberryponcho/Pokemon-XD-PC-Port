#include "launcher_modupdate.h"

#include <i18n.h>
#include <modsign.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace launcher::modupd {

namespace em = engine::mods;
namespace mu = engine::modupdate;
namespace mc = engine::modcheck;
namespace fs = std::filesystem;
using engine::i18n::Tr;

namespace {

std::string Fmt(const char* fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  return buf;
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string ReadAll(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

const em::ModFolder* FindMod(const std::vector<em::ModFolder>* found, const std::string& id) {
  if (found == nullptr) return nullptr;
  for (const em::ModFolder& m : *found) {
    if (Lower(m.manifest.id) == Lower(id)) return &m;
  }
  return nullptr;
}

std::string TagText(const std::vector<std::string>& games) {
  const std::string tag = em::GameTag(games);
  return tag.empty() ? std::string() : " [" + tag + "]";
}

std::string Mega(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / 1e6);
  return text;
}

void Wrapped(const ImVec4& color, const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

}

Updater::~Updater() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void Updater::Init(const Host& host) {
  host_ = host;
  if (!host_.testRun) {
    std::error_code ec;
    std::filesystem::remove_all(Folders().Temp(), ec);
  }
  choices_ = mu::LoadChoices(Folders());
  keys_ = mu::LoadKeys(Folders());
}

mu::Folders Updater::Folders() const {
  mu::Folders f;
  f.modsFolder = host_.modsFolder;
  f.stateFolder = host_.stateFolder;
  return f;
}

void Updater::SaveChoices() {
  if (!host_.testRun) mu::SaveChoices(Folders(), choices_);
}

bool Updater::WaitPending() const {
  if (busy_.load()) return true;
  return serial_.load() != seen_;
}

bool Updater::Offers(const std::string& id, std::string& version, std::string* note) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Item& it : items_) {
    if (Lower(it.id) != Lower(id) || it.isNew || !it.offer.error.empty() || !it.offer.newer) continue;
    version = it.offer.file.version;
    if (note != nullptr && it.offer.trust.trust == mu::Trust::Refused) *note = it.offer.trust.text;
    return true;
  }
  return false;
}

mu::PreviousInfo Updater::Previous(const std::string& id) const { return mu::Previous(Folders(), id); }

void Updater::RequestCheck(bool manual) {
  if (busy_.load()) return;
  manualCheck_ = manual;
  if (!choices_.asked) {
    checkAfterAnswer_ = true;
    openQuestion_ = true;
    return;
  }
  StartCheck();
}

void Updater::OnStart() {
  if (choices_.always) {
    manualCheck_ = false;
    StartCheck();
  }
}

void Updater::StartCheck() {
  if (busy_.load() || host_.found == nullptr) return;
  if (thread_.joinable()) thread_.join();
  struct Job {
    std::string id, name, version, authorKey, link;
    fs::path folder;
  };
  std::vector<Job> jobs;
  for (const em::ModFolder& m : *host_.found) {
    if (m.texturePack || m.manifest.updates.empty()) continue;
    jobs.push_back({m.manifest.id, m.manifest.name, m.manifest.version, m.manifest.authorKey, m.manifest.updates, m.folder});
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    phase_ = Phase::Checking;
    items_.clear();
    notes_.clear();
  }
  Log("checking " + std::to_string(jobs.size()) + " mod(s) for updates (the player said yes)");
  busy_ = true;
  stop_ = false;
  const bool manual = manualCheck_;
  thread_ = std::thread([this, jobs, manual] {
    std::vector<Item> items;
    std::vector<std::string> notes;
    const mu::KeyStore keys = mu::LoadKeys(Folders());
    for (const Job& j : jobs) {
      if (stop_.load()) break;
      Item it;
      it.id = j.id;
      it.name = j.name;
      it.installedVersion = j.version;
      it.folder = j.folder;
      it.authorKey = j.authorKey;
      Log("asking " + mu::HostOf(j.link) + " about " + j.id);
      it.offer = mu::FetchOffer(j.link, j.id, j.version, j.folder, j.authorKey, keys, host_.rules, [this] { return stop_.load(); });
      items.push_back(std::move(it));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    items_ = std::move(items);
    notes_ = std::move(notes);
    phase_ = Phase::Results;
    manualCheck_ = manual;
    serial_++;
    busy_ = false;
  });
}

void Updater::StartFetchNew(const std::string& id, const std::string& link) {
  if (busy_.load()) return;
  if (thread_.joinable()) thread_.join();
  const em::ModFolder* installed = FindMod(host_.found, id);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    phase_ = Phase::Checking;
    items_.clear();
    notes_.clear();
  }
  Log("asking " + mu::HostOf(link) + " for " + id + " (the player said yes)");
  busy_ = true;
  stop_ = false;
  const std::string version = installed != nullptr ? installed->manifest.version : std::string();
  const fs::path folder = installed != nullptr ? installed->folder : fs::path();
  const std::string authorKey = installed != nullptr ? installed->manifest.authorKey : std::string();
  const std::string name = installed != nullptr ? installed->manifest.name : id;
  manualCheck_ = true;
  thread_ = std::thread([this, id, link, version, folder, authorKey, name] {
    Item it;
    it.id = id;
    it.name = name;
    it.installedVersion = version;
    it.folder = folder;
    it.isNew = version.empty();
    it.authorKey = authorKey;
    const mu::KeyStore keys = mu::LoadKeys(Folders());
    it.offer = mu::FetchOffer(link, id, version, folder, authorKey, keys, host_.rules, [this] { return stop_.load(); });
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
    items_.push_back(std::move(it));
    phase_ = Phase::Results;
    serial_++;
    busy_ = false;
  });
}

void Updater::StartDownload(size_t index) {
  if (busy_.load()) return;
  if (host_.testRun) {
    if (host_.say) host_.say("Test run on the player's own files: nothing is downloaded or installed.");
    return;
  }
  if (thread_.joinable()) thread_.join();
  Item item;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= items_.size()) return;
    item = items_[index];
    current_ = index;
    phase_ = Phase::Downloading;
  }
  done_ = 0;
  total_ = item.offer.file.size;
  Log("downloading " + item.id + " " + item.offer.file.version + " from " + mu::HostOf(item.offer.file.url));
  busy_ = true;
  stop_ = false;
  thread_ = std::thread([this, item] {
    mu::Staged staged = mu::Download(Folders(), item.offer, host_.rules, [this](uint64_t d, uint64_t t) {
      done_ = d;
      total_ = t;
      return !stop_.load();
    });
    mu::Change change;
    std::vector<mu::Playthrough> users;
    if (!staged.ok) {
      mu::DropStaged(staged);
      staged.root.clear();
    }
    if (staged.ok && !item.isNew) {
      change = mu::Analyse(item.folder, staged.folder);
      if (host_.cardRoots) users = mu::PlaythroughsUsing(host_.cardRoots(), item.id);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    staged_ = std::move(staged);
    change_ = change;
    users_ = users;
    failure_ = staged_.ok ? std::string() : staged_.error;
    phase_ = staged_.ok ? Phase::Staged : Phase::Failed;
    Log(staged_.ok ? "download checked: ok (" + staged_.antivirus + ")" : "download refused: " + staged_.error);
    serial_++;
    busy_ = false;
  });
}

void Updater::FinishInstall(bool) {
  Item item;
  mu::Staged staged;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ >= items_.size()) return;
    item = items_[current_];
    staged = staged_;
    phase_ = Phase::Idle;
  }
  const bool pin = mu::PinsKey(item.offer.trust.trust) || (item.offer.trust.trust == mu::Trust::NeedsConfirm && item.keyConfirmed);
  if (item.offer.trust.trust == mu::Trust::NeedsConfirm && !item.keyConfirmed) {
    if (host_.say) host_.say(Tr("The new signing key was not confirmed, so nothing was installed."));
    mu::DropStaged(staged);
    return;
  }
  const mu::InstallResult r = mu::Install(Folders(), staged, item.id, item.isNew ? fs::path() : item.folder, pin ? item.offer.file.publicKey : std::string());
  if (!r.ok) {
    if (host_.say) host_.say(Fmt(Tr("Couldn't install %s: %s. Nothing was changed."), item.name.c_str(), r.error.c_str()));
    Log("install failed: " + r.error);
    mu::DropStaged(staged);
    return;
  }
  keys_ = mu::LoadKeys(Folders());
  if (item.offer.trust.trust == mu::Trust::KeyChanged || item.offer.trust.trust == mu::Trust::NeedsConfirm) {
    Log("signing key of " + item.id + " changed from " + mc::KeyFingerprint(item.offer.trust.oldKey) + " to " + mc::KeyFingerprint(item.offer.trust.newKey) +
        (item.offer.trust.trust == mu::Trust::KeyChanged ? " (announced by the old key)" : " (confirmed by the player)"));
  }
  Log("installed " + item.id + " " + item.offer.file.version + (r.replaced ? " (version " + item.installedVersion + " kept for roll back)" : ""));
  if (host_.say) {
    host_.say(r.replaced ? Fmt(Tr("%s is now version %s. The old version (%s) is kept: use \"Roll back\" to go back to it."), item.name.c_str(),
                               item.offer.file.version.c_str(), item.installedVersion.c_str())
                         : Fmt(Tr("%s %s is installed. It is switched off until you turn it on."), item.name.c_str(), item.offer.file.version.c_str()));
  }
  if (host_.reload) host_.reload();
  if (!pendingTurnOn_.empty() && !importing_) {
    const std::string again = pendingTurnOn_;
    pendingTurnOn_.clear();
    BeginTurnOn(again);
  }
  if (importing_) NextImportDownload();
}

void Updater::BeginTurnOn(const std::string& id) {
  if (host_.mods == nullptr || host_.found == nullptr) return;
  const em::ModFolder* mod = FindMod(host_.found, id);
  if (mod == nullptr) return;
  std::vector<std::string> on;
  for (const auto& [mid, enabled] : host_.mods->Active().mods) {
    if (enabled) on.push_back(mid);
  }
  const std::string game = host_.discGameId ? host_.discGameId() : std::string();
  em::Requirements req = em::ResolveRequirements(*mod, *host_.found, on, game);
  if (req.turnOn.empty() && req.missing.empty() && req.conflicts.empty()) {
    TurnOnNow(id, {});
    return;
  }
  needs_ = Needs{};
  needs_.id = id;
  needs_.name = mod->manifest.name;
  needs_.req = std::move(req);
  for (const em::ModFolder* m : needs_.req.turnOn) needs_.turnOnIds.push_back(m->manifest.id);
  openNeeds_ = true;
}

void Updater::TurnOnNow(const std::string& id, const std::vector<std::string>& more) {
  if (host_.mods == nullptr || host_.found == nullptr) return;
  em::ModList::Profile& profile = host_.mods->Active();
  const auto turn = [&](const std::string& which) {
    for (auto& [mid, on] : profile.mods) {
      if (Lower(mid) == Lower(which)) on = true;
    }
  };
  for (const std::string& m : more) turn(m);
  turn(id);
  em::OrderResult order;
  const bool moved = em::SortProfile(profile, *host_.found, &order);
  if (host_.saveList) host_.saveList();
  std::string text = more.empty() ? Fmt(Tr("Turned on %s."), id.c_str())
                                  : Fmt(Tr("Turned on %s and the %d mod(s) it needs."), id.c_str(), static_cast<int>(more.size()));
  if (moved) text += " " + std::string(Tr("The list was put in a working order."));
  if (!order.cycle.empty()) text += " " + std::string(Tr("Some mods each want to come after the other, so their order is yours to decide."));
  if (host_.say) host_.say(text);
  Log("turned on " + id + (more.empty() ? "" : " + " + std::to_string(more.size()) + " required"));
}

void Updater::SortList(bool say) {
  if (host_.mods == nullptr || host_.found == nullptr) return;
  em::OrderResult order;
  const bool moved = em::SortProfile(host_.mods->Active(), *host_.found, &order);
  if (moved && host_.saveList) host_.saveList();
  if (say && host_.say) {
    host_.say(moved ? std::string(Tr("The mod list was put in a working order (requirements first, then \"after\" / \"before\", then priority)."))
                    : std::string(Tr("The mod list already is in a working order.")));
    if (!order.cycle.empty()) host_.say(Tr("Some mods each want to come after the other; their order is yours to decide."));
  }
}

void Updater::AskRollBack(const std::string& id) {
  rollbackId_ = id;
  openRollback_ = true;
}

void Updater::ExportList(const std::string& file) {
  if (host_.mods == nullptr || host_.found == nullptr) return;
  const std::string text = mu::ExportList(host_.mods->Active(), *host_.found, host_.mods->active, host_.discGameId ? host_.discGameId() : std::string());
  std::ofstream out(fs::path(std::u8string(file.begin(), file.end())), std::ios::binary);
  out << text;
  out.close();
  if (host_.say) host_.say(out ? Fmt(Tr("The mod list was saved to %s."), file.c_str()) : Fmt(Tr("The list couldn't be saved to %s."), file.c_str()));
  Log("exported the list to " + file);
}

void Updater::ImportList(const std::string& file) {
  const std::string text = ReadAll(fs::path(std::u8string(file.begin(), file.end())));
  mu::SharedList list;
  std::string error;
  if (!mu::ParseList(text, list, error)) {
    if (host_.say) host_.say(Fmt(Tr("That file can't be used as a mod list: %s."), error.c_str()));
    return;
  }
  list_ = list;
  RePlan();
  openImport_ = true;
  Log("read a mod list with " + std::to_string(plan_.rows.size()) + " mod(s)");
}

void Updater::RePlan() {
  plan_ = mu::PlanImport(list_, host_.found != nullptr ? *host_.found : std::vector<em::ModFolder>(), host_.discGameId ? host_.discGameId() : std::string());
}

void Updater::StartExport() {
  if (!host_.chooseFile) return;
  host_.chooseFile("mod_list.json", true, [this](const std::string& path) { ExportList(path); });
}

void Updater::StartImport() {
  if (!host_.chooseFile) return;
  host_.chooseFile("", false, [this](const std::string& path) { ImportList(path); });
}

void Updater::NextImportDownload() {
  while (!importQueue_.empty()) {
    const size_t at = importQueue_.front();
    importQueue_.erase(importQueue_.begin());
    if (at >= plan_.rows.size() || plan_.rows[at].link.empty()) continue;
    StartFetchNew(plan_.rows[at].mod.id, plan_.rows[at].link);
    return;
  }
  importing_ = false;
  RePlan();
  if (host_.say) host_.say(Tr("The downloads from the list are finished. Look at the list again and apply it when you are ready."));
  openImport_ = true;
}

void Updater::DoStep(const std::string& verb, const std::string& arg, bool& ok) {
  ok = true;
  const std::string what = Lower(arg);
  if (verb == "modupdate") {
    if (what == "check") {
      RequestCheck(true);
    } else if (what == "yes" || what == "always" || what == "no") {
      if (what != "no") choices_.always = what == "always";
      choices_.asked = true;
      SaveChoices();
      openQuestion_ = false;
      Log(std::string("answer: ") + what);
      if (what != "no" && checkAfterAnswer_) StartCheck();
      checkAfterAnswer_ = false;
      closeQuestion_ = true;
    } else if (what == "get") {
      std::lock_guard<std::mutex> lock(mutex_);
      size_t first = items_.size();
      for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].offer.error.empty() && items_[i].offer.newer && Allowed(items_[i])) {
          first = i;
          break;
        }
      }
      if (first == items_.size()) {
        ok = false;
      } else {
        phase_ = Phase::Results;
        pendingGet_ = static_cast<int>(first);
      }
    } else if (what == "install" || what == "keep") {
      installAnswer_ = what == "install" ? 1 : 2;
    } else if (what == "skip") {
      skipAnswer_ = true;
    } else if (what == "confirmkey") {
      std::lock_guard<std::mutex> lock(mutex_);
      for (Item& it : items_) {
        if (it.offer.trust.trust == mu::Trust::NeedsConfirm) it.keyConfirmed = true;
      }
      Log("new signing key confirmed (script)");
    } else if (what == "close") {
      closeAll_ = true;
    } else if (what == "rollbackyes") {
      rollbackAnswer_ = 1;
    } else if (what.rfind("rollback=", 0) == 0) {
      AskRollBack(arg.substr(9));
    } else {
      ok = false;
    }
  } else if (verb == "modneeds") {
    if (what == "yes") needsAnswer_ = 1;
    else if (what == "download") needsAnswer_ = 2;
    else if (what == "no") needsAnswer_ = 3;
    else BeginTurnOn(arg);
  } else if (verb == "modlist") {
    if (what.rfind("export=", 0) == 0) ExportList(arg.substr(7));
    else if (what.rfind("import=", 0) == 0) ImportList(arg.substr(7));
    else if (what == "apply") importAnswer_ = 1;
    else if (what == "download") importAnswer_ = 2;
    else if (what == "close") importAnswer_ = 3;
    else ok = false;
  } else if (verb == "modsort") {
    SortList(true);
  } else {
    ok = false;
  }
}

void Updater::Poll() {
  if (serial_.load() == seen_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  seen_ = serial_.load();
  if (phase_ == Phase::Results) {
    bool any = !items_.empty() && (manualCheck_ || importing_ || !pendingTurnOn_.empty());
    for (const Item& it : items_) {
      const auto skipped = choices_.skipped.find(it.id);
      const bool skippedThis = skipped != choices_.skipped.end() && skipped->second == it.offer.file.version;
      if (it.offer.error.empty() && it.offer.newer && !skippedThis) any = true;
    }
    openResults_ = any;
    if (!any) {
      phase_ = Phase::Idle;
      if (host_.say) host_.say(Tr("All your mods with an update link are up to date, or nothing could be checked."));
    }
  } else if (phase_ == Phase::Staged) {
    openStaged_ = true;
  } else if (phase_ == Phase::Failed) {
    openFailed_ = true;
  }
}

void Updater::Draw(const Ui& ui) {
  const float wrap = 600 * ui.scale;
  const bool closeAll = ui.closeNow ? ui.closeNow() : false;

  if (openQuestion_) {
    ImGui::OpenPopup("###modupq");
    openQuestion_ = false;
    closeQuestion_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("Check for mod updates?")) + "###modupq").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll || closeQuestion_) {
      ImGui::CloseCurrentPopup();
      closeQuestion_ = false;
    }
    ImGui::PushTextWrapPos(wrap);
    ui.heading(Tr("Ask the mods' authors for newer versions?"));
    ImGui::TextUnformatted(Tr("This contacts the web addresses that your mods name as their \"update link\" (only https:// ones), and "
                              "nothing else. It only asks whether a newer version exists. Nothing is downloaded or installed without "
                              "another yes from you, and every update is scanned and shown to you first."));
    {
      std::string hosts;
      if (host_.found != nullptr) {
        for (const em::ModFolder& m : *host_.found) {
          if (m.manifest.updates.empty()) continue;
          const std::string h = mu::HostOf(m.manifest.updates);
          if (!h.empty() && hosts.find(h) == std::string::npos) hosts += (hosts.empty() ? "" : ", ") + h;
        }
      }
      ui.dimText(hosts.empty() ? std::string(Tr("None of your mods has an update link yet.")) : Fmt(Tr("Right now: %s."), hosts.c_str()));
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const auto answer = [&](bool yes, bool always) {
      choices_.asked = true;
      choices_.always = yes && always;
      SaveChoices();
      ImGui::CloseCurrentPopup();
      Log(std::string("answer: ") + (!yes ? "no" : always ? "always" : "yes"));
      if (yes && checkAfterAnswer_) StartCheck();
      checkAfterAnswer_ = false;
    };
    if (ui.primary(Tr("Yes, check now"))) answer(true, false);
    ImGui::SameLine();
    if (ImGui::Button(Tr("Yes, and always check when the launcher starts"))) answer(true, true);
    ImGui::SameLine();
    if (ImGui::Button(Tr("No")) || ui.back()) answer(false, false);
    ImGui::EndPopup();
  }

  if (openResults_) {
    ImGui::OpenPopup("###modupr");
    openResults_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("Mod updates")) + "###modupr").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll || closeAll_) {
      ImGui::CloseCurrentPopup();
      closeAll_ = false;
    }
    std::vector<Item> items;
    Phase phase;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      items = items_;
      phase = phase_;
    }
    int getIndex = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      getIndex = pendingGet_;
      pendingGet_ = -1;
    }
    ImGui::PushTextWrapPos(wrap);
    if (phase == Phase::Downloading) {
      ui.heading(Tr("Downloading and checking..."));
      const uint64_t d = done_.load(), t = total_.load();
      ImGui::ProgressBar(t != 0 ? static_cast<float>(static_cast<double>(d) / static_cast<double>(t)) : 0.0f, ImVec2(wrap, 0),
                         (Mega(d) + " / " + Mega(t)).c_str());
      ui.dimText(Tr("The download is checked against the signed update file, scanned, and only then shown to you."));
      if (ImGui::Button(Tr("Cancel"))) stop_ = true;
    } else if (phase == Phase::Checking) {
      ui.heading(Tr("Asking..."));
    } else {
      ui.heading(Tr("Mod updates"));
      bool anything = false;
      for (size_t i = 0; i < items.size(); i++) {
        const Item& it = items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        const std::string title = (it.name.empty() ? it.id : it.name) + TagText(it.offer.file.games);
        if (!it.offer.error.empty()) {
          Wrapped(ui.warn, Fmt(Tr("%s: %s"), title.c_str(), it.offer.error.c_str()));
        } else if (!it.offer.newer) {
          ui.dimText(Fmt(Tr("%s %s: you have the newest version."), title.c_str(), it.installedVersion.c_str()));
        } else {
          anything = true;
          const bool refused = it.offer.trust.trust == mu::Trust::Refused;
          const bool needsKey = it.offer.trust.trust == mu::Trust::NeedsConfirm && !it.keyConfirmed;
          ui.heading((it.isNew ? Fmt(Tr("%s %s (not installed yet)"), title.c_str(), it.offer.file.version.c_str())
                               : Fmt(Tr("%s: %s to %s"), title.c_str(), it.installedVersion.c_str(), it.offer.file.version.c_str()))
                         .c_str());
          ui.dimText(Fmt(Tr("From %s. Download: %s."), mu::HostOf(it.offer.file.url).c_str(), Mega(it.offer.file.size).c_str()));
          if (!it.offer.file.notes.empty()) {
            ImGui::TextUnformatted(Tr("What's new:"));
            ImGui::BeginChild("##notes", ImVec2(wrap, std::min(120 * ui.scale, ImGui::GetTextLineHeightWithSpacing() * 7)),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(it.offer.file.notes.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
          }
          ImGui::TextUnformatted(Tr("What this mod may do (permissions):"));
          if (it.offer.file.permissions.empty()) ui.dimText(Tr("  nothing beyond changing a few files"));
          for (const std::string& code : it.offer.file.permissions) {
            const bool added = std::find(it.offer.addedPermissions.begin(), it.offer.addedPermissions.end(), code) != it.offer.addedPermissions.end() &&
                               !it.isNew;
            if (added) {
              Wrapped(ui.bad, Fmt(Tr("  NEW in this update: %s"), mu::PermissionText(code).c_str()));
            } else {
              ui.dimText("  " + mu::PermissionText(code));
            }
          }
          if (!it.isNew && !it.offer.addedPermissions.empty()) {
            std::string names;
            for (const std::string& c : it.offer.addedPermissions) names += (names.empty() ? "" : "; ") + mu::PermissionText(c);
            Wrapped(ui.warn, Fmt(Tr("This update asks for something the version you have did not: %s. Only go on if you are happy with that."), names.c_str()));
          }
          Wrapped(refused ? ui.bad : it.offer.trust.trust == mu::Trust::FirstUse || it.offer.trust.trust == mu::Trust::NeedsConfirm ? ui.warn : ui.good,
                  it.offer.trust.text);
          if (it.offer.trust.trust == mu::Trust::NeedsConfirm) {
            Wrapped(ui.bad, Fmt(Tr("Key you had: %s    New key: %s"), mc::KeyFingerprint(it.offer.trust.oldKey).c_str(),
                                mc::KeyFingerprint(it.offer.trust.newKey).c_str()));
          }
          if (needsKey) {
            if (ImGui::Button(Tr("I checked this fingerprint with the author: trust the new key"))) {
              std::lock_guard<std::mutex> lock(mutex_);
              if (i < items_.size()) items_[i].keyConfirmed = true;
              Log("new signing key of " + it.id + " confirmed by the player: " + mc::KeyFingerprint(it.offer.trust.newKey));
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Skip this version"))) {
              choices_.skipped[it.id] = it.offer.file.version;
              SaveChoices();
              Log("skip " + it.id + " " + it.offer.file.version);
            }
          } else if (!refused) {
            const bool getThis = ui.primary(Tr("Download and check")) || getIndex == static_cast<int>(i);
            ImGui::SameLine();
            if (ImGui::Button(Tr("Skip this version")) || skipAnswer_) {
              choices_.skipped[it.id] = it.offer.file.version;
              SaveChoices();
              skipAnswer_ = false;
              Log("skip " + it.id + " " + it.offer.file.version);
            }
            if (getThis) {
              ImGui::PopID();
              ImGui::PopTextWrapPos();
              StartDownload(i);
              ImGui::EndPopup();
              return;
            }
          }
        }
        ImGui::PopID();
      }
      if (!anything && !items.empty()) ui.dimText(Tr("Nothing to install."));
      ImGui::Spacing();
      ui.dimText(Tr("These checks lower the risk; they cannot prove a mod harmless. Only install mods from people you trust."));
      if (ImGui::Button(Tr("Close")) || ui.back()) {
        ImGui::CloseCurrentPopup();
        pendingTurnOn_.clear();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }
  {
    Phase p;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      p = phase_;
    }
    if (p == Phase::Downloading && !ImGui::IsPopupOpen("###modupr")) ImGui::OpenPopup("###modupr");
  }

  if (openStaged_) {
    ImGui::OpenPopup("###modupi");
    openStaged_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("Install this update?")) + "###modupi").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll) ImGui::CloseCurrentPopup();
    Item item;
    mu::Staged staged;
    mu::Change change;
    std::vector<mu::Playthrough> users;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (current_ < items_.size()) item = items_[current_];
      staged = staged_;
      change = change_;
      users = users_;
    }
    bool keepsNumbers = false;
    for (const mu::Playthrough& p : users) keepsNumbers = keepsNumbers || p.keepsNumbers;
    const bool protect = change.worldChanged && !users.empty();
    ImGui::PushTextWrapPos(wrap);
    ui.heading(Fmt(Tr("%s %s"), (item.name + TagText(staged.report.manifestOk ? staged.report.manifest.games : item.offer.file.games)).c_str(),
                   item.offer.file.version.c_str()).c_str());
    Wrapped(ui.good, Tr("The download matches the signed update file (size and fingerprint) and passed the safety scan and the checker."));
    ui.dimText(Fmt(Tr("Virus scan: %s."), staged.antivirus.c_str()));
    if (!staged.findings.empty()) {
      ImGui::TextUnformatted(Tr("The checker has remarks (none of them stops the install):"));
      ImGui::BeginChild("##findings", ImVec2(wrap, std::min(140 * ui.scale, ImGui::GetTextLineHeightWithSpacing() * 8)),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted(staged.findings.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndChild();
    }
    ImGui::TextUnformatted(Tr("Permissions (as the scan found them in the files):"));
    for (const std::string& code : staged.actualPermissions) {
      const bool added = std::find(item.offer.addedPermissions.begin(), item.offer.addedPermissions.end(), code) != item.offer.addedPermissions.end() && !item.isNew;
      if (added) Wrapped(ui.bad, Fmt(Tr("  NEW: %s"), mu::PermissionText(code).c_str()));
      else ui.dimText("  " + mu::PermissionText(code));
    }
    if (staged.actualPermissions.empty()) ui.dimText(Tr("  nothing beyond changing a few files"));
    if (protect) {
      ImGui::Separator();
      ui.heading(Tr("Your saves use this mod"));
      std::string saves;
      for (const mu::Playthrough& p : users) {
        saves += "\n  - " + p.save + " (" + p.label + (p.version.empty() ? "" : ", version " + p.version) + (p.keepsNumbers ? std::string(", ") + Tr("keeps its saved numbers") : std::string()) + ")";
      }
      Wrapped(ui.warn, Fmt(Tr("This update changes what a playthrough depends on. Playthroughs that were started with %s:%s"), item.name.c_str(), saves.c_str()));
      for (const std::string& r : change.reasons) Wrapped(ui.warn, "  " + r);
      if (keepsNumbers) {
        Wrapped(ui.warn, Tr("Some of these saves keep numbers this mod's scripts saved (streaks, points). A new version reads them as they are: if it counts them another way, "
                            "a streak or a point total can come out wrong. Keep the current version for those saves unless the notes say the numbers carry over."));
      }
      Wrapped(ui.warn, change.addedRowsBreak
                           ? Tr("If you install it, the game will refuse to CONTINUE those saves (it never loads a save whose rows moved), until you roll back to the old version. "
                                "Your save files are not touched either way.")
                           : Tr("If you install it, CONTINUE on those saves asks you first, because the mod is no longer the version they started with. "
                                "Your save files are not touched either way."));
    } else if (change.worldChanged) {
      ui.dimText(Tr("It changes game data, but none of your saves was started with this mod."));
    }
    ImGui::Spacing();
    const auto close = [&] {
      ImGui::CloseCurrentPopup();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = Phase::Idle;
      }
    };
    int answer = installAnswer_;
    installAnswer_ = 0;
    if (protect) {
      if (ui.primary(Tr("Keep my current version")) || answer == 2) {
        mu::DropStaged(staged);
        if (host_.say) host_.say(Fmt(Tr("Kept %s %s: the update was not installed, so your saves can still use it."), item.name.c_str(), item.installedVersion.c_str()));
        Log("kept the current version of " + item.id + " (playthroughs use it)");
        close();
      }
      ImGui::SameLine();
      if (ImGui::Button(Tr("Install anyway (the old version is kept)")) || answer == 1) {
        close();
        FinishInstall(true);
      }
    } else {
      if (ui.primary(Tr("Install")) || answer == 1) {
        close();
        FinishInstall(true);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel")) || ui.back() || (!protect && answer == 2)) {
      mu::DropStaged(staged);
      close();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openFailed_) {
    ImGui::OpenPopup("###modupf");
    openFailed_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("The update was not installed")) + "###modupf").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll) ImGui::CloseCurrentPopup();
    std::string why, findings;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      why = failure_;
      findings = staged_.findings;
    }
    ImGui::PushTextWrapPos(wrap);
    Wrapped(ui.bad, why);
    if (!findings.empty()) {
      ImGui::BeginChild("##failfindings", ImVec2(wrap, std::min(220 * ui.scale, ImGui::GetTextLineHeightWithSpacing() * 14)),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted(findings.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndChild();
    }
    ui.dimText(Tr("Nothing was changed: the version you have stays as it is."));
    if (ImGui::Button(Tr("OK")) || ui.back()) {
      ImGui::CloseCurrentPopup();
      mu::DropStaged(staged_);
      std::lock_guard<std::mutex> lock(mutex_);
      phase_ = Phase::Idle;
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openRollback_) {
    ImGui::OpenPopup("###modupb");
    openRollback_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("Go back to the earlier version?")) + "###modupb").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll) ImGui::CloseCurrentPopup();
    const em::ModFolder* mod = FindMod(host_.found, rollbackId_);
    const mu::PreviousInfo prev = mu::Previous(Folders(), rollbackId_);
    ImGui::PushTextWrapPos(wrap);
    if (mod == nullptr || !prev.exists) {
      ImGui::TextUnformatted(Tr("There is no earlier version of this mod to go back to."));
      if (ImGui::Button(Tr("OK")) || ui.back()) ImGui::CloseCurrentPopup();
    } else {
      ui.heading((mod->manifest.name + TagText(mod->manifest.games)).c_str());
      ImGui::TextUnformatted(Fmt(Tr("You have version %s. The launcher kept version %s%s. Going back swaps them: the version you have now is kept, so you can go forward again."),
                                 mod->manifest.version.c_str(), prev.version.c_str(), prev.date.empty() ? "" : (" (from " + prev.date + ")").c_str())
                                 .c_str());
      if (host_.cardRoots) {
        const auto users = mu::PlaythroughsUsing(host_.cardRoots(), rollbackId_);
        if (!users.empty()) {
          std::string saves;
          for (const mu::Playthrough& p : users) saves += (saves.empty() ? "" : ", ") + p.save + (p.version.empty() ? "" : " (version " + p.version + ")");
          ui.dimText(Fmt(Tr("Saves started with this mod: %s. The game checks each one against the mod's version when you CONTINUE."), saves.c_str()));
        }
      }
      const int answer = rollbackAnswer_;
      rollbackAnswer_ = 0;
      if (ui.primary(Tr("Go back")) || answer == 1) {
        std::string error;
        if (host_.testRun) {
          if (host_.say) host_.say("Test run on the player's own files: nothing is changed.");
        } else if (mu::RollBack(Folders(), rollbackId_, mod->folder, error)) {
          if (host_.say) host_.say(Fmt(Tr("%s is back to version %s."), mod->manifest.name.c_str(), prev.version.c_str()));
          Log("rolled back " + rollbackId_ + " to " + prev.version);
          if (host_.reload) host_.reload();
        } else if (host_.say) {
          host_.say(error);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(Tr("Cancel")) || ui.back()) ImGui::CloseCurrentPopup();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openNeeds_) {
    ImGui::OpenPopup("###modupn");
    openNeeds_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("This mod needs other mods")) + "###modupn").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll) ImGui::CloseCurrentPopup();
    ImGui::PushTextWrapPos(wrap);
    ui.heading(needs_.name.c_str());
    if (!needs_.req.turnOn.empty()) {
      ImGui::TextUnformatted(Tr("These mods are in your mods folder but switched off. They will be turned on too:"));
      for (const em::ModFolder* m : needs_.req.turnOn) ui.dimText("  " + m->manifest.name + " " + m->manifest.version);
    }
    int downloadIndex = -1;
    for (size_t i = 0; i < needs_.req.missing.size(); i++) {
      const auto& miss = needs_.req.missing[i];
      std::string want = miss.id;
      if (!miss.op.empty()) want += " " + miss.op + " " + miss.version;
      Wrapped(ui.warn, miss.have.empty() ? Fmt(Tr("Missing: %s (needed by %s). It isn't in your mods folder."), want.c_str(), miss.neededBy.c_str())
                                         : Fmt(Tr("Too old: you have %s %s, but %s is needed."), miss.id.c_str(), miss.have.c_str(), want.c_str()));
      if (!miss.link.empty()) {
        std::string why;
        if (mu::UrlAllowed(miss.link, why)) {
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::Button(Fmt(Tr("Download from %s..."), mu::HostOf(miss.link).c_str()).c_str()) || (needsAnswer_ == 2 && downloadIndex < 0)) downloadIndex = static_cast<int>(i);
          ImGui::PopID();
        } else {
          ui.dimText(Fmt(Tr("The mod's link can't be used: %s."), why.c_str()));
        }
      } else {
        ui.dimText(Tr("The mod names no place to get it. Look for it where you got this mod."));
      }
    }
    for (const std::string& c : needs_.req.conflicts) Wrapped(ui.bad, c);
    const int answer = needsAnswer_;
    needsAnswer_ = 0;
    if (downloadIndex >= 0) {
      const auto& miss = needs_.req.missing[static_cast<size_t>(downloadIndex)];
      pendingTurnOn_ = needs_.id;
      ImGui::CloseCurrentPopup();
      importing_ = false;
      StartFetchNew(miss.id, miss.link);
    } else {
      ImGui::Spacing();
      const bool can = needs_.req.missing.empty() && needs_.req.conflicts.empty();
      if (!can) ImGui::BeginDisabled();
      if (ui.primary(needs_.req.turnOn.empty() ? Tr("Turn it on") : Tr("Turn them all on")) || (answer == 1 && can)) {
        TurnOnNow(needs_.id, needs_.turnOnIds);
        ImGui::CloseCurrentPopup();
      }
      if (!can) ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button(Tr("Cancel")) || ui.back() || answer == 3) ImGui::CloseCurrentPopup();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openImport_) {
    ImGui::OpenPopup("###modupl");
    openImport_ = false;
  }
  if (ImGui::BeginPopupModal((std::string(Tr("Mod list from a file")) + "###modupl").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeAll) ImGui::CloseCurrentPopup();
    ImGui::PushTextWrapPos(wrap);
    ui.heading(Tr("These mods, in this order"));
    ui.dimText(Tr("Nothing has been changed or run yet. Applying only turns on the mods you already have and puts them in this order."));
    if (!plan_.gameNote.empty()) Wrapped(ui.warn, plan_.gameNote);
    int missing = 0, downloadable = 0;
    for (size_t i = 0; i < plan_.rows.size(); i++) {
      const mu::ImportRow& r = plan_.rows[i];
      const em::ModFolder* haveMod = FindMod(host_.found, r.mod.id);
      const std::string name = (r.mod.name.empty() ? r.mod.id : r.mod.name) + (haveMod != nullptr ? TagText(haveMod->manifest.games) : std::string());
      if (r.state == mu::ImportRow::State::Have) {
        Wrapped(ui.good, Fmt(Tr("%d. %s %s: you have it"), static_cast<int>(i) + 1, name.c_str(), r.mod.version.c_str()));
      } else if (r.state == mu::ImportRow::State::OtherVersion) {
        Wrapped(ui.warn, Fmt(Tr("%d. %s: the list has version %s, you have %s%s"), static_cast<int>(i) + 1, name.c_str(), r.mod.version.c_str(),
                             r.haveVersion.c_str(), r.link.empty() ? "" : " (an update link is known)"));
        downloadable += r.link.empty() ? 0 : 1;
      } else {
        missing++;
        downloadable += r.link.empty() ? 0 : 1;
        Wrapped(ui.bad, Fmt(Tr("%d. %s %s: missing%s"), static_cast<int>(i) + 1, name.c_str(), r.mod.version.c_str(),
                            r.link.empty() ? "" : Fmt(Tr(" (can be downloaded from %s, after your yes)"), mu::HostOf(r.link).c_str()).c_str()));
      }
    }
    const int answer = importAnswer_;
    importAnswer_ = 0;
    ImGui::Spacing();
    if (ui.primary(Tr("Apply the order")) || answer == 1) {
      RePlan();
      if (host_.mods != nullptr) {
        const std::vector<std::string> gone = mu::ApplyImport(host_.mods->Active(), plan_);
        if (host_.saveList) host_.saveList();
        if (host_.say) {
          host_.say(gone.empty() ? std::string(Tr("The list was applied.")) : Fmt(Tr("The list was applied; %d mod(s) from it are missing."), static_cast<int>(gone.size())));
        }
        Log("applied a shared list (" + std::to_string(gone.size()) + " missing)");
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (downloadable == 0) ImGui::BeginDisabled();
    if (ImGui::Button(Fmt(Tr("Download %d..."), downloadable).c_str()) || (answer == 2 && downloadable > 0)) {
      importQueue_.clear();
      for (size_t i = 0; i < plan_.rows.size(); i++) {
        if (plan_.rows[i].state != mu::ImportRow::State::Have && !plan_.rows[i].link.empty()) importQueue_.push_back(i);
      }
      importing_ = true;
      ImGui::CloseCurrentPopup();
      NextImportDownload();
    }
    if (downloadable == 0) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Tr("Close")) || ui.back() || answer == 3) ImGui::CloseCurrentPopup();
    (void)missing;
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }
}

}
