#ifndef XD_SCRIPT_EXT_H
#define XD_SCRIPT_EXT_H

#include <mod_values.h>
#include <script_host.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct PortMenuPage;
struct PortMenuInput;

namespace xd::scriptext {

engine::mods::ModValues& Values();
void SetVersions(const std::map<std::string, std::string>& versionOf);
void LoadValues(const std::vector<engine::mods::ModValueSet>& sets, const char* why);
void ClearValues(const char* why);

std::vector<engine::mods::ModValueSet> ExportValues();
bool ValuesChanged(const std::vector<engine::mods::ModValueSet>& kept);

void Reset();
void RegisterApis(engine::script::Host& host);
engine::script::Value BattleRulesTable(const std::string& mod, const std::vector<engine::script::Value>& args, std::string& error);

using StoryCaller = bool (*)(const std::string& mod, const std::string& name, const std::vector<engine::script::Value>& args);
void SetStoryCaller(StoryCaller caller);
void SetInStoryCall(bool on);
void SetArming(bool on);

void RunPending();
void DrawPage();
bool PageUp();

bool HasMenuEntries();
size_t MenuEntryCount();
const char* MenuEntryLabel(size_t i);
const char* MenuEntryMod(size_t i);
void MenuEntryRun(size_t i);
bool EntryPageUp();
void EntryPageFrame(const PortMenuInput* in);
void EntryPageReset();

void BattleStarted();
void BattleEnded();

}

extern "C" {
void PortXD_ScriptRunPending(void);
}

#endif
