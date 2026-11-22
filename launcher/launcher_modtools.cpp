#include "launcher_modtools.h"

#include <i18n.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace launcher::modtools {

namespace em = engine::mods;
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

std::string Mega(uint64_t bytes) {
  char text[32];
  if (bytes < 1000000) std::snprintf(text, sizeof text, "%.0f KB", static_cast<double>(bytes) / 1e3);
  else std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / 1e6);
  return text;
}

std::string WhereText(const mc::Finding& f) {
  std::string w = f.file.empty() ? std::string(Tr("the mod")) : f.file;
  if (f.line > 0) w += Fmt(Tr(" line %d"), f.line);
  return w + ": " + f.text;
}

int sFindingRow = 0;

void FindingRow(const std::string& text, ImVec4 color, float width) {
  ImGui::PushID(sFindingRow++);
  const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, width);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::Selectable("##finding", false, 0, ImVec2(0, size.y));
  ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, ImGui::ColorConvertFloat4ToU32(color), text.c_str(),
                                      nullptr, width);
  ImGui::PopID();
}

}

Tools::~Tools() {
  if (thread_.joinable()) thread_.join();
}

void Tools::Init(const Host& host) { host_ = host; }

const em::ModFolder* Tools::Find(const std::string& id) const {
  if (host_.found == nullptr) return nullptr;
  for (const em::ModFolder& m : *host_.found) {
    if (Lower(m.manifest.id) == Lower(id)) return &m;
  }
  return nullptr;
}

std::string Tools::Verdict(const mc::Report& report) {
  const size_t problems = report.Count(mc::Severity::Problem), warnings = report.Count(mc::Severity::Warning),
               notes = report.Count(mc::Severity::Note);
  if (problems == 0 && warnings == 0) {
    return notes == 0 ? std::string(Tr("Nothing wrong found."))
                      : Fmt(Tr("Nothing wrong found (notes to read: %d)."), static_cast<int>(notes));
  }
  if (problems == 0) return Fmt(Tr("No problems, but there are warnings to read: %d."), static_cast<int>(warnings));
  return Fmt(Tr("Problems: %d, warnings: %d. The mod may not work, or may not be safe, as it is."), static_cast<int>(problems),
             static_cast<int>(warnings));
}

std::string Tools::DeepNote(const mc::Report& report, bool discGiven, const std::string& discWhy) {
  if (report.deepDone) {
    size_t found = 0;
    for (const mc::Finding& f : report.findings) found += f.code == "game-loader";
    return found == 0 ? std::string(Tr("Checked against your disc: the game's own loader read the mod and found nothing wrong (table and field names, number ranges)."))
                      : Fmt(Tr("Checked against your disc: the game's own loader read the mod and said %d things (below)."), static_cast<int>(found));
  }
  if (!discGiven) return Tr("Not checked against a disc: choose your disc on the Disc page and check again to test table and field names and number ranges.");
  return Fmt(Tr("The disc could not be used for the deeper check (%s). The other checks ran."), discWhy.c_str());
}

void Tools::StartCheck(const std::string& id) {
  const em::ModFolder* mod = Find(id);
  if (mod == nullptr || busy_.load()) return;
  Job job;
  job.kind = Kind::Check;
  job.id = mod->manifest.id;
  job.name = mod->manifest.name.empty() ? mod->manifest.id : mod->manifest.name;
  job.version = mod->manifest.version;
  job.folder = mod->folder;
  Start(job);
}

void Tools::BeforeOn(const std::string& id) {
  const em::ModFolder* mod = Find(id);
  if (mod == nullptr || mod->texturePack || busy_.load()) {
    if (host_.turnOn) host_.turnOn(id);
    return;
  }
  if (cleanIds_.count(mod->manifest.id + "|" + mod->manifest.version)) {
    if (host_.turnOn) host_.turnOn(id);
    return;
  }
  Job job;
  job.kind = Kind::BeforeOn;
  job.id = mod->manifest.id;
  job.name = mod->manifest.name.empty() ? mod->manifest.id : mod->manifest.name;
  job.version = mod->manifest.version;
  job.folder = mod->folder;
  Start(job);
}

void Tools::StartPack(const std::string& id) {
  const em::ModFolder* mod = Find(id);
  if (mod == nullptr || busy_.load() || !host_.chooseZip) return;
  std::string name = mod->manifest.id + (mod->manifest.version.empty() ? "" : "-" + mod->manifest.version) + ".zip";
  const std::string modId = mod->manifest.id;
  host_.chooseZip(name, [this, modId](const std::string& path) { PackTo(modId, path); });
}

void Tools::PackTo(const std::string& id, const std::string& zipPath) {
  const em::ModFolder* mod = Find(id);
  if (mod == nullptr || zipPath.empty() || busy_.load()) return;
  Job job;
  job.kind = Kind::Pack;
  job.id = mod->manifest.id;
  job.name = mod->manifest.name.empty() ? mod->manifest.id : mod->manifest.name;
  job.version = mod->manifest.version;
  job.folder = mod->folder;
  job.zipPath = zipPath;
  Start(job);
}

void Tools::Start(const Job& job) {
  if (thread_.joinable()) thread_.join();
  busy_ = true;
  Log(std::string(job.kind == Kind::Pack ? "pack " : job.kind == Kind::BeforeOn ? "check before switching on " : "check ") + job.id);
  thread_ = std::thread([this, job] { Run(job); });
}

void Tools::Run(Job job) {
  Result result;
  result.job = job;
  mc::Options options;
  options.folder = job.folder;
  options.rules = host_.rules;
  options.modsFolder = host_.modsFolder;
  em::LoadOptions load;
  bool opened = false;
  const std::string disc = host_.discPath ? host_.discPath() : std::string();
  result.discGiven = !disc.empty();
  if (result.discGiven) {
    if (!host_.openDisc) {
      result.discWhy = Tr("this game has no deeper check");
    } else {
      for (int i = 0; i < 100 && host_.discBusy && host_.discBusy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::string error;
      if (host_.discBusy && host_.discBusy()) {
        result.discWhy = Tr("the launcher is still reading the disc; try again in a moment");
      } else if (host_.openDisc(disc, load, error)) {
        opened = true;
        options.deep = &load;
      } else {
        result.discWhy = error;
      }
    }
  }
  if (job.kind == Kind::Pack) {
    std::error_code ec;
    mc::PackOptions pack;
    pack.check = options;
    pack.outDir = (host_.scratchFolder.empty() ? fs::temp_directory_path(ec) : host_.scratchFolder) / ("pack_" + job.id);
    fs::remove_all(pack.outDir, ec);
    fs::create_directories(pack.outDir, ec);
    if (opened) pack.disc = &load.disc;
    result.pack = mc::PackMod(pack);
    result.report = result.pack.report;
    if (result.pack.ok) {
      const fs::path target = fs::path(job.zipPath);
      fs::create_directories(target.parent_path(), ec);
      fs::copy_file(result.pack.zip, target, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        result.copyError = ec.message();
      } else {
        result.packed = true;
        result.finalZip = target;
        if (!result.pack.updateFile.empty()) {
          std::error_code ec2;
          fs::copy_file(result.pack.updateFile, target.parent_path() / result.pack.updateFile.filename(),
                        fs::copy_options::overwrite_existing, ec2);
        }
      }
    }
    fs::remove_all(pack.outDir, ec);
  } else {
    result.report = mc::CheckMod(options);
  }
  if (opened && host_.closeDisc) host_.closeDisc();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = std::move(result);
  }
  busy_ = false;
  serial_++;
}

void Tools::Poll() {
  if (serial_.load() == seen_) return;
  seen_ = serial_.load();
  std::lock_guard<std::mutex> lock(mutex_);
  const Result& r = result_;
  Log("done: " + r.job.id + ": " + Verdict(r.report) + (r.job.kind == Kind::Pack ? (r.packed ? " packed" : " not packed") : ""));
  if (r.job.kind == Kind::BeforeOn) {
    const bool readable = r.report.Count(mc::Severity::Problem) + r.report.Count(mc::Severity::Warning) > 0 || r.report.Count(mc::Severity::Note, mc::Category::Safety) > 0;
    if (!readable) {
      cleanIds_.insert(r.job.id + "|" + r.job.version);
      if (host_.say) host_.say(Fmt(Tr("Checked \"%s\" before switching it on: nothing wrong found."), r.job.name.c_str()));
      if (host_.turnOn) host_.turnOn(r.job.id);
      return;
    }
  }
  openWindow_ = true;
}

Tools::Last Tools::LastResult() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Last last;
  last.id = result_.job.id;
  last.verdict = Verdict(result_.report);
  last.problems = result_.report.Count(mc::Severity::Problem) > 0;
  last.packed = result_.packed;
  last.zip = result_.finalZip;
  return last;
}

bool Tools::WaitPending() const { return busy_.load() || openWindow_ || serial_.load() != seen_; }

void Tools::DoStep(const std::string& verb, const std::string& arg, bool& ok) {
  if (verb == "modcheck") {
    if (Lower(arg) == "close") answer_ = 2;
    else StartCheck(arg);
  } else if (verb == "modpack") {
    const size_t eq = arg.find('=');
    if (eq == std::string::npos) ok = false;
    else PackTo(arg.substr(0, eq), arg.substr(eq + 1));
  } else if (verb == "modon") {
    BeforeOn(arg);
  } else if (verb == "modtools") {
    const std::string what = Lower(arg);
    if (what == "on") answer_ = 1;
    else if (what == "close" || what == "keep") answer_ = 2;
    else ok = false;
  } else {
    ok = false;
  }
}

void Tools::Draw(const launcher::modupd::Ui& ui) {
  if (openWindow_) {
    ImGui::OpenPopup("###modtool");
    openWindow_ = false;
    windowOpen_ = true;
  }
  if (!windowOpen_) return;
  Result r;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    r = result_;
  }
  const float wrap = 640 * ui.scale;
  const std::string title = (r.job.kind == Kind::Pack ? Fmt(Tr("Pack: %s"), r.job.name.c_str()) : Fmt(Tr("Check: %s"), r.job.name.c_str())) + "###modtool";
  ImGui::SetNextWindowSizeConstraints(ImVec2(wrap + 40 * ui.scale, 0), ImVec2(wrap + 40 * ui.scale, 900 * ui.scale));
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    windowOpen_ = false;
    return;
  }
  const bool closeScript = ui.closeNow ? ui.closeNow() : false;
  const mc::Report& report = r.report;
  const bool problems = report.Count(mc::Severity::Problem) > 0;
  const bool isOn = r.job.kind == Kind::BeforeOn;
  bool closeIt = false, switchOn = false;

  ImGui::PushTextWrapPos(wrap);
  {
    const std::string tag = report.manifestOk ? em::GameTag(report.manifest) : std::string();
    std::string head = r.job.name + (r.job.version.empty() ? "" : " " + r.job.version) + (tag.empty() ? "" : "  [" + tag + "]");
    ui.heading(head.c_str());
  }
  if (r.job.kind == Kind::Pack) {
    if (r.packed) {
      ImGui::PushStyleColor(ImGuiCol_Text, ui.good);
      ImGui::TextWrapped("%s", Fmt(Tr("Packed. The zip is at %s (%s)."), r.finalZip.string().c_str(), Mega(r.pack.size).c_str()).c_str());
      ImGui::PopStyleColor();
      ui.dimText(Fmt(Tr("SHA-256: %s. %d files in it, %d left out as junk."), r.pack.sha256.c_str(), static_cast<int>(r.pack.included.size()),
                     static_cast<int>(r.pack.leftOut.size())));
      if (!r.pack.updateFile.empty()) ui.dimText(Fmt(Tr("The update file %s was written next to the zip."), r.pack.updateFile.filename().string().c_str()));
      ui.dimText(Tr("To sign an update file with your own key, use xd_mod_pack on the command line (see the sharing guide)."));
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ui.bad);
      ImGui::TextWrapped("%s", r.copyError.empty() ? Tr("Nothing was packed. The mod has to be fixed first:")
                                                   : Fmt(Tr("The zip was made but could not be saved there (%s)."), r.copyError.c_str()).c_str());
      ImGui::PopStyleColor();
      if (!r.pack.error.empty()) ImGui::TextWrapped("%s", r.pack.error.c_str());
    }
    if (!r.pack.refused.empty()) {
      ui.heading(Tr("Left out of the zip, because it is the game's own data or yours"));
      for (const mc::Refusal& f : r.pack.refused) ImGui::TextWrapped("%s: %s", f.path.c_str(), f.reason.c_str());
    }
    for (const std::string& m : r.pack.messages) ui.dimText(m);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, problems ? ui.bad : report.Count(mc::Severity::Warning) > 0 ? ui.warn : ui.good);
    ImGui::TextWrapped("%s", Verdict(report).c_str());
    ImGui::PopStyleColor();
  }
  ui.dimText(DeepNote(report, r.discGiven, r.discWhy));
  ImGui::PopTextWrapPos();

  if (!report.support.empty()) ui.dimText(Fmt(Tr("Works with: %s"), em::SupportText(report.support, false).c_str()));

  const auto section = [&](mc::Category cat, mc::Severity sev, const char* heading, ImVec4 color) {
    std::vector<const mc::Finding*> list;
    for (const mc::Finding& f : report.findings) {
      if (f.category == cat && f.severity == sev) list.push_back(&f);
    }
    if (list.empty()) return;
    ui.heading(heading);
    std::stable_sort(list.begin(), list.end(), [](const mc::Finding* a, const mc::Finding* b) { return a->file < b->file; });
    for (const mc::Finding* f : list) FindingRow(WhereText(*f), color, ImGui::GetContentRegionAvail().x - 6 * ui.scale);
  };
  const bool anything = !report.findings.empty() || !report.permissions.empty();
  if (anything && ImGui::BeginChild("##findings", ImVec2(wrap, std::min(360.0f * ui.scale, 40 * ui.scale + 26 * ui.scale * (report.findings.size() + report.permissions.size() + 4))),
                                    ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened)) {
    sFindingRow = 0;
    section(mc::Category::Safety, mc::Severity::Problem, Tr("Safety problems"), ui.bad);
    section(mc::Category::Safety, mc::Severity::Warning, Tr("Safety warnings"), ui.warn);
    section(mc::Category::Safety, mc::Severity::Note, Tr("Safety notes"), ui.dim);
    section(mc::Category::Mod, mc::Severity::Problem, Tr("Problems"), ui.bad);
    section(mc::Category::Mod, mc::Severity::Warning, Tr("Warnings"), ui.warn);
    section(mc::Category::Mod, mc::Severity::Note, Tr("Notes"), ui.dim);
    if (!report.permissions.empty()) {
      ui.heading(Tr("What this mod can do"));
      for (const mc::Permission& p : report.permissions) FindingRow("- " + p.text, ui.dim, ImGui::GetContentRegionAvail().x - 6 * ui.scale);
    }
  }
  if (anything) ImGui::EndChild();
  else ui.dimText(Tr("No findings at all."));
  if (report.findings.empty() && report.Count(mc::Severity::Problem, mc::Category::Safety) == 0 && r.job.kind != Kind::Pack) {
    ui.dimText(Tr("This lowers the risk; it cannot prove a mod is harmless. Only use mods from people you trust."));
  }
  ImGui::Spacing();
  if (isOn) {
    if (problems) {
      if (ui.primary(Tr("Keep it off"))) closeIt = true;
      ImGui::SetItemDefaultFocus();
      ImGui::SameLine();
      if (ImGui::Button(Tr("Switch it on anyway"))) switchOn = true;
    } else {
      if (ui.primary(Tr("Switch it on"))) switchOn = true;
      ImGui::SetItemDefaultFocus();
      ImGui::SameLine();
      if (ImGui::Button(Tr("Keep it off"))) closeIt = true;
    }
  } else {
    if (ui.primary(Tr("Close"))) closeIt = true;
    ImGui::SetItemDefaultFocus();
  }
  ImGui::SameLine();
  if (ImGui::Button(Tr("Copy the report"))) {
    std::string text = report.Format();
    if (r.job.kind == Kind::Pack) text += "\n" + r.pack.Format();
    ImGui::SetClipboardText(text.c_str());
  }
  if (r.job.kind == Kind::Pack && r.packed && host_.openFolder) {
    ImGui::SameLine();
    if (ImGui::Button(Tr("Show the zip"))) host_.openFolder(r.finalZip.parent_path());
  }
  if (ui.back()) closeIt = true;
  if (closeScript || answer_ == 2) closeIt = true;
  if (answer_ == 1 && isOn) switchOn = true;
  if (closeIt || switchOn) {
    answer_ = 0;
    ImGui::CloseCurrentPopup();
    windowOpen_ = false;
    if (isOn && switchOn) {
      if (!problems) cleanIds_.insert(r.job.id + "|" + r.job.version);
      Log("switching on after the check: " + r.job.id);
      if (host_.turnOn) host_.turnOn(r.job.id);
    } else if (isOn) {
      Log("kept off after the check: " + r.job.id);
    }
  }
  ImGui::EndPopup();
}

}
