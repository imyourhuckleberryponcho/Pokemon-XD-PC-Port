#pragma once

#include "launcher_icon.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher::sys {

void Start();
void End();

std::filesystem::path ProgramFile();
std::filesystem::path ProgramFolder();

std::FILE* OpenFile(const std::filesystem::path& file, const char* mode);

std::filesystem::path EnvPath(const char* name);

std::string ProgramName(const std::string& stem);

struct FontSet {
  std::filesystem::path regular, strong, display;
};
std::vector<FontSet> SystemFonts();

struct StartRequest {
  std::filesystem::path program;
  std::vector<std::string> arguments;
  std::filesystem::path folder;
  bool shareOutput = false;
  bool hidden = false;
  bool lowPriority = false;
  bool ownEnvironment = false;
  std::vector<std::string> environmentWithout;
  std::vector<std::string> environmentAdd;
  bool toFront = false;
  std::filesystem::path outputFile;
  bool killTree = false;
};
struct Process;
Process* StartProgram(const StartRequest& request, std::string& error);
bool Ended(Process* process, int waitMs);
int ExitCode(Process* process);
unsigned long IdOf(Process* process);
void Stop(Process* process);
void Close(Process* process);

bool OpenInSystem(const std::filesystem::path& what);

enum class Folder { Documents, Desktop, Programs };
std::filesystem::path KnownFolder(Folder which, bool create);

bool ToRecycleBin(const std::filesystem::path& folder, std::string& error);
const char* RecycleBinName();
const char* UserNameWords();
const char* HomeFolderExample();

bool MakeShortcut(const std::filesystem::path& folder, const std::string& name, const std::string& description,
                  const std::filesystem::path& target, const std::filesystem::path& iconFile, std::string& message);
const char* ProgramsFolderName();
bool ShortcutsHere();
bool HasIcons();

icon::Image ReadIcoSize(const std::filesystem::path& file, int size);

}
