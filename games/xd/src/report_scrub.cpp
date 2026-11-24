#include "report_scrub.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace xd::report {

namespace {

bool IsSep(char c) { return c == '/' || c == '\\'; }
bool NameEnd(char c) {
  return IsSep(c) || c == '"' || c == '\'' || c == ',' || c == ';' || c == ':' || c == '\n' || c == '\r' || c == '\t' || c == '<' || c == '>' ||
         c == '|' || c == '*' || c == '?' || c == ')' || c == ']' || c == '}' || c == '\0';
}

char Low(char c, bool fold) { return fold && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

size_t FindFold(const std::string& text, const std::string& what, size_t from, bool fold) {
  if (what.empty() || text.size() < what.size()) return std::string::npos;
  for (size_t i = from; i + what.size() <= text.size(); i++) {
    size_t k = 0;
    while (k < what.size() && Low(text[i + k], fold) == Low(what[k], fold)) k++;
    if (k == what.size()) return i;
  }
  return std::string::npos;
}

template <typename Accept>
std::string ReplaceIf(const std::string& text, const std::string& what, const std::string& with, bool fold, Accept accept) {
  std::string out;
  size_t at = 0, from = 0;
  for (size_t i = FindFold(text, what, from, fold); i != std::string::npos; i = FindFold(text, what, from, fold)) {
    if (accept(text, i, i + what.size())) {
      out.append(text, at, i - at);
      out += with;
      at = from = i + what.size();
    } else {
      from = i + 1;
    }
  }
  out.append(text, at, std::string::npos);
  return out;
}

std::vector<std::string> Forms(std::string folder) {
  while (!folder.empty() && IsSep(folder.back())) folder.pop_back();
  std::string fwd = folder, back = folder, doubled;
  std::replace(fwd.begin(), fwd.end(), '\\', '/');
  std::replace(back.begin(), back.end(), '/', '\\');
  for (char c : back) doubled += c == '\\' ? std::string("\\\\") : std::string(1, c);
  std::vector<std::string> forms;
  for (const std::string& f : {doubled, back, fwd}) {
    if (std::find(forms.begin(), forms.end(), f) == forms.end()) forms.push_back(f);
  }
  return forms;
}

std::string ReplaceUserFolders(const std::string& text, const std::string& dir, bool windows, const std::string& with) {
  std::string out;
  size_t at = 0, from = 0;
  for (size_t i = FindFold(text, dir, from, windows); i != std::string::npos; i = FindFold(text, dir, from, windows)) {
    from = i + 1;
    if (i == 0 || !IsSep(text[i - 1])) continue;
    size_t k = i + dir.size();
    if (k >= text.size() || !IsSep(text[k])) continue;
    size_t begin = i - 1;
    while (begin > 0 && IsSep(text[begin - 1])) begin--;
    if (windows && begin >= 2 && text[begin - 1] == ':' && std::isalpha(static_cast<unsigned char>(text[begin - 2])) != 0) begin -= 2;
    if (begin < at) continue;
    while (k < text.size() && IsSep(text[k])) k++;
    const size_t nameStart = k;
    while (k < text.size() && !NameEnd(text[k])) k++;
    while (k < text.size() && text[k] == ' ') {
      size_t m = k + 1;
      while (m < text.size() && !NameEnd(text[m]) && text[m] != ' ') m++;
      if (m < text.size() && IsSep(text[m]) && m > k + 1) k = m; else break;
    }
    if (k == nameStart) continue;
    out.append(text, at, begin - at);
    out += with;
    at = from = k;
  }
  out.append(text, at, std::string::npos);
  return out;
}

}

Home HomeOfThisAccount() {
  Home h;
  auto env = [](const char* n) { const char* v = std::getenv(n); return v != nullptr ? std::string(v) : std::string(); };
#if defined(_WIN32)
  h.windows = true;
  const std::string profile = env("USERPROFILE");
  if (!profile.empty()) h.folders.push_back(profile);
  const std::string homePath = env("HOMEPATH");
  if (!homePath.empty()) h.folders.push_back(env("HOMEDRIVE") + homePath);
  h.userName = env("USERNAME");
#else
  const std::string home = env("HOME");
  if (!home.empty()) h.folders.push_back(home);
  h.userName = env("USER");
#endif
  return h;
}

std::string Scrub(const std::string& text, const Home& who) {
  const std::string home = who.windows ? "%USERPROFILE%" : "~";
  std::string out = text;
  std::vector<std::string> folders = who.folders;
  std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
  for (const std::string& folder : folders) {
    if (folder.size() < 3 || (folder.size() == 3 && folder[1] == ':')) continue;
    for (const std::string& form : Forms(folder)) {
      out = ReplaceIf(out, form, home, who.windows, [](const std::string& t, size_t, size_t end) {
        return end == t.size() || NameEnd(t[end]) || t[end] == ' ';
      });
    }
  }
  out = ReplaceUserFolders(out, "Users", who.windows, home);
  if (!who.windows) out = ReplaceUserFolders(out, "home", false, home);
  if (who.userName.size() >= 2) {
    out = ReplaceIf(out, who.userName, who.windows ? "%USERNAME%" : "~user", who.windows, [](const std::string& t, size_t start, size_t end) { return start > 0 && IsSep(t[start - 1]) && (end == t.size() || NameEnd(t[end])); });
  }
  return out;
}

void ScrubReportFolder(const std::filesystem::path& folder) {
  const Home who = HomeOfThisAccount();
  for (const wchar_t* name : {L"info.txt", L"log.txt", L"recording.txt", L"settings.ini", L"mods.ini"}) {
    const std::filesystem::path file = folder / name;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) continue;
    std::string text;
    {
      std::ifstream in(file, std::ios::binary);
      if (!in) continue;
      text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string clean = Scrub(text, who);
    if (clean == text) continue;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(clean.data(), static_cast<std::streamsize>(clean.size()));
  }
}

}
