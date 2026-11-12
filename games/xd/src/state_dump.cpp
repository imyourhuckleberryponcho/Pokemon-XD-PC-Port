#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#ifdef XD_DIA_DLL
#include <dia2.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

extern "C" void (*PortInput_SetFrameHook(void (*hook)(uint32_t frame)))(uint32_t frame);
extern "C" uint32_t* __hsd_rand_seed_hold;
extern void* MEM1Start;

namespace {

struct Probe {
  std::string label, path, type;
  uint32_t count = 1;
  std::vector<uintptr_t> offsets;
  uint32_t size = 4;
  std::string fields;
  const uint8_t* address = nullptr;
};

std::vector<Probe> sProbes;
std::set<uint32_t> sFrames;
uint32_t sRangeFrom = 0, sRangeTo = 0, sRangeStep = 0;
FILE* sOut = nullptr;
uint32_t sSeedValue = 0, sSeedFrame = 0;
bool sSeedPending = false;

constexpr uint32_t kMem1Size = 0x1800000;

uint32_t fnv1a(const uint8_t* data, uint32_t size) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < size; i++) h = (h ^ data[i]) * 16777619u;
  return h;
}

std::string expand_fields(const std::string& f) {
  std::string out;
  for (size_t i = 0; i < f.size();) {
    const char c = f[i++];
    size_t n = 0, digits = 0;
    while (i < f.size() && f[i] >= '0' && f[i] <= '9') n = n * 10 + static_cast<size_t>(f[i++] - '0'), digits++;
    out.append(digits != 0 ? n : 1, c);
  }
  return out;
}

uint32_t type_size(const std::string& t) {
  if (t.rfind("rec:", 0) == 0) {
    uint32_t n = 0;
    for (char c : t.substr(4)) n += c == 'H' ? 2 : c == 'I' ? 4 : 1;
    return n;
  }
  if (t == "u8" || t == "s8") return 1;
  if (t == "u16" || t == "s16") return 2;
  return 4;
}

ULONG plain_type(HANDLE process, ULONG64 base, ULONG typeId) {
  for (int guard = 0; guard < 8; guard++) {
    DWORD tag = 0;
    SymGetTypeInfo(process, base, typeId, TI_GET_SYMTAG, &tag);
    if (tag != 17 && tag != 7) break;
    ULONG inner = 0;
    if (!SymGetTypeInfo(process, base, typeId, TI_GET_TYPEID, &inner)) break;
    typeId = inner;
  }
  return typeId;
}

struct TypeList {
  std::vector<ULONG> ids;
};

BOOL CALLBACK collect_type(PSYMBOL_INFO info, ULONG, PVOID context) {
  static_cast<TypeList*>(context)->ids.push_back(info->TypeIndex);
  return TRUE;
}

void list_by_name(HANDLE process, ULONG64 base, const char* name, TypeList* out) {
  SymEnumTypesByName(process, base, name, collect_type, out);
}

bool find_member(HANDLE process, ULONG64 base, ULONG typeId, const std::string& name, ULONG* offset,
                 ULONG* memberType) {
  typeId = plain_type(process, base, typeId);
  DWORD count = 0;
  if (!SymGetTypeInfo(process, base, typeId, TI_GET_CHILDRENCOUNT, &count) || count == 0) {
    return false;
  }
  std::vector<uint8_t> buf(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
  auto* children = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(buf.data());
  children->Count = count;
  children->Start = 0;
  if (!SymGetTypeInfo(process, base, typeId, TI_FINDCHILDREN, children)) {
    return false;
  }
  for (DWORD i = 0; i < count; i++) {
    WCHAR* wname = nullptr;
    if (!SymGetTypeInfo(process, base, children->ChildId[i], TI_GET_SYMNAME, &wname) || wname == nullptr) {
      continue;
    }
    char narrow[256];
    WideCharToMultiByte(CP_UTF8, 0, wname, -1, narrow, sizeof(narrow), nullptr, nullptr);
    LocalFree(wname);
    if (name == narrow) {
      DWORD off = 0;
      SymGetTypeInfo(process, base, children->ChildId[i], TI_GET_OFFSET, &off);
      ULONG t = 0;
      SymGetTypeInfo(process, base, children->ChildId[i], TI_GET_TYPEID, &t);
      *offset = off;
      *memberType = t;
      return true;
    }
  }
  return false;
}

std::vector<ULONG> candidates(HANDLE process, ULONG64 base, ULONG typeId) {
  typeId = plain_type(process, base, typeId);
  std::vector<ULONG> out{typeId};
  WCHAR* wname = nullptr;
  if (!SymGetTypeInfo(process, base, typeId, TI_GET_SYMNAME, &wname) || wname == nullptr) return out;
  char narrow[256];
  WideCharToMultiByte(CP_UTF8, 0, wname, -1, narrow, sizeof(narrow), nullptr, nullptr);
  LocalFree(wname);
  TypeList list;
  list_by_name(process, base, narrow, &list);
  std::vector<std::pair<DWORD, ULONG>> others;
  for (ULONG id : list.ids) {
    if (id == typeId) continue;
    DWORD count = 0;
    SymGetTypeInfo(process, base, id, TI_GET_CHILDRENCOUNT, &count);
    if (count != 0) others.push_back({count, id});
  }
  std::stable_sort(others.begin(), others.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (auto& o : others) out.push_back(o.second);
  if (std::getenv("XD_STATE_DEBUG") != nullptr) {
    std::fprintf(stderr, "[state dump]   type %s (%lu): %zu found by name, %zu with members\n", narrow, typeId,
                 list.ids.size(), others.size());
  }
  return out;
}

bool resolve_from(HANDLE process, ULONG64 base, const std::string& s, size_t i, ULONG type,
                  std::vector<uintptr_t>& offsets, std::string& error) {
  if (i == std::string::npos || i >= s.size()) return true;
  if (s[i] == '@') {
    size_t next = s.find_first_of(".[-@", i + 1);
    std::string name = s.substr(i + 1, next == std::string::npos ? std::string::npos : next - i - 1);
    TypeList list;
    if (name.size() > 2 && name.front() == '(' && name.back() == ')') {
      std::string fn = name.substr(1, name.size() - 2);
      alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
      std::memset(buffer, 0, sizeof(buffer));
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      DWORD count = 0;
      if (SymFromName(process, fn.c_str(), sym) &&
          SymGetTypeInfo(process, base, sym->TypeIndex, TI_GET_CHILDRENCOUNT, &count) && count > 0) {
        std::vector<uint8_t> buf(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
        auto* children = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(buf.data());
        children->Count = count;
        children->Start = 0;
        ULONG argType = 0, pointer = 0, pointee = 0;
        if (SymGetTypeInfo(process, base, sym->TypeIndex, TI_FINDCHILDREN, children) &&
            SymGetTypeInfo(process, base, children->ChildId[0], TI_GET_TYPEID, &argType)) {
          pointer = plain_type(process, base, argType);
          if (SymGetTypeInfo(process, base, pointer, TI_GET_TYPEID, &pointee)) list.ids.push_back(pointee);
        }
      }
    } else {
      list_by_name(process, base, name.c_str(), &list);
    }
    if (list.ids.empty()) {
      error = "type " + name + " not found";
      return false;
    }
    for (ULONG id : list.ids) {
      for (ULONG candidate : candidates(process, base, id)) {
        if (resolve_from(process, base, s, next, candidate, offsets, error)) return true;
      }
    }
    return false;
  }
  if (s[i] == '[') {
    size_t close = s.find(']', i);
    uint32_t index = static_cast<uint32_t>(std::strtoul(s.c_str() + i + 1, nullptr, 0));
    ULONG t = plain_type(process, base, type);
    DWORD tag = 0;
    SymGetTypeInfo(process, base, t, TI_GET_SYMTAG, &tag);
    ULONG elem = 0;
    ULONG64 length = 0;
    if (tag != 15 || !SymGetTypeInfo(process, base, t, TI_GET_TYPEID, &elem) ||
        !SymGetTypeInfo(process, base, elem, TI_GET_LENGTH, &length)) {
      error = "[" + std::to_string(index) + "] on something that isn't an array";
      return false;
    }
    offsets.back() += static_cast<uintptr_t>(index * length);
    if (resolve_from(process, base, s, close == std::string::npos ? close : close + 1, elem, offsets, error)) {
      return true;
    }
    offsets.back() -= static_cast<uintptr_t>(index * length);
    return false;
  }
  bool arrow = s.compare(i, 2, "->") == 0;
  size_t from = i + (arrow ? 2 : 1);
  size_t next = s.find_first_of(".[-@", from);
  std::string name = s.substr(from, next == std::string::npos ? std::string::npos : next - from);
  if (arrow) {
    ULONG t = plain_type(process, base, type);
    DWORD tag = 0;
    SymGetTypeInfo(process, base, t, TI_GET_SYMTAG, &tag);
    ULONG pointee = 0;
    if (tag != 14 || !SymGetTypeInfo(process, base, t, TI_GET_TYPEID, &pointee)) {
      error = "->" + name + " on something that isn't a pointer";
      return false;
    }
    type = pointee;
    offsets.push_back(0);
    if (!name.empty() && name[0] == '+') {
      offsets.back() += static_cast<uintptr_t>(std::strtoul(name.c_str() + 1, nullptr, 0));
      if (resolve_from(process, base, s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
    if (name.empty()) {
      if (resolve_from(process, base, s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
  }
  bool found = false;
  for (ULONG candidate : candidates(process, base, type)) {
    ULONG offset = 0, memberType = 0;
    if (!find_member(process, base, candidate, name, &offset, &memberType)) continue;
    found = true;
    offsets.back() += offset;
    if (resolve_from(process, base, s, next, memberType, offsets, error)) return true;
    offsets.back() -= offset;
  }
  if (!found) error = "member " + name + " not found";
  if (arrow) offsets.pop_back();
  return false;
}

#ifdef XD_DIA_DLL
IDiaSession* sDia = nullptr;
IDiaSymbol* sDiaGlobal = nullptr;
bool sDiaTried = false;

bool dia_open() {
  if (sDiaTried) return sDiaGlobal != nullptr;
  sDiaTried = true;
  HMODULE dll = LoadLibraryW(XD_DIA_DLL);
  if (dll == nullptr) {
    std::fprintf(stderr, "[state dump] DIA not available (%lu): records by dbghelp\n", GetLastError());
    return false;
  }
  using GetClassObject = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);
  auto get = reinterpret_cast<GetClassObject>(GetProcAddress(dll, "DllGetClassObject"));
  IClassFactory* factory = nullptr;
  if (get == nullptr || FAILED(get(__uuidof(DiaSource), IID_IClassFactory, reinterpret_cast<void**>(&factory)))) {
    return false;
  }
  IDiaDataSource* source = nullptr;
  HRESULT hr = factory->CreateInstance(nullptr, __uuidof(IDiaDataSource), reinterpret_cast<void**>(&source));
  factory->Release();
  if (FAILED(hr)) return false;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  hr = source->loadDataForExe(exe, nullptr, nullptr);
  if (FAILED(hr) || FAILED(source->openSession(&sDia))) {
    std::fprintf(stderr, "[state dump] DIA could not read the program's debug info (0x%08lx)\n", hr);
    return false;
  }
  sDia->put_loadAddress(reinterpret_cast<ULONGLONG>(GetModuleHandleW(nullptr)));
  sDia->get_globalScope(&sDiaGlobal);
  return sDiaGlobal != nullptr;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

DWORD dia_tag(IDiaSymbol* s) {
  DWORD t = 0;
  if (s != nullptr) s->get_symTag(&t);
  return t;
}

IDiaSymbol* dia_type(IDiaSymbol* s) {
  IDiaSymbol* t = nullptr;
  if (s != nullptr) s->get_type(&t);
  return t;
}

IDiaSymbol* dia_plain(IDiaSymbol* t) {
  for (int guard = 0; guard < 8 && t != nullptr && dia_tag(t) == SymTagTypedef; guard++) t = dia_type(t);
  return t;
}

std::vector<IDiaSymbol*> dia_children(IDiaSymbol* parent, enum SymTagEnum tag, const std::string& name) {
  std::vector<IDiaSymbol*> out;
  IDiaEnumSymbols* e = nullptr;
  std::wstring w = widen(name);
  if (parent == nullptr || FAILED(parent->findChildren(tag, name.empty() ? nullptr : w.c_str(), nsNone, &e)) ||
      e == nullptr) {
    return out;
  }
  IDiaSymbol* s = nullptr;
  ULONG got = 0;
  while (SUCCEEDED(e->Next(1, &s, &got)) && got == 1) out.push_back(s);
  e->Release();
  return out;
}

std::string dia_name(IDiaSymbol* s) {
  BSTR b = nullptr;
  std::string out;
  if (s != nullptr && s->get_name(&b) == S_OK && b != nullptr) {
    for (const wchar_t* c = b; *c; c++) out += static_cast<char>(*c);
    SysFreeString(b);
  }
  return out;
}

std::vector<std::pair<std::string, IDiaSymbol*>> sDiaRecords;

std::vector<IDiaSymbol*> dia_records(const std::string& name) {
  if (sDiaRecords.empty()) {
    for (IDiaSymbol* u : dia_children(sDiaGlobal, SymTagUDT, "")) sDiaRecords.push_back({dia_name(u), u});
    if (std::getenv("XD_STATE_DEBUG") != nullptr) {
      std::fprintf(stderr, "[state dump]   DIA: %zu records\n", sDiaRecords.size());
    }
  }
  std::vector<IDiaSymbol*> out;
  for (auto& r : sDiaRecords) {
    if (r.first == name) out.push_back(r.second);
  }
  return out;
}

std::vector<IDiaSymbol*> dia_candidates(IDiaSymbol* type) {
  type = dia_plain(type);
  std::vector<IDiaSymbol*> out{type};
  if (dia_tag(type) != SymTagUDT) return out;
  DWORD self = 0;
  type->get_symIndexId(&self);
  std::vector<std::pair<size_t, IDiaSymbol*>> others;
  for (IDiaSymbol* u : dia_records(dia_name(type))) {
    DWORD id = 0;
    u->get_symIndexId(&id);
    if (id == self) continue;
    size_t members = dia_children(u, SymTagData, "").size();
    if (members != 0) others.push_back({members, u});
  }
  if (std::getenv("XD_STATE_DEBUG") != nullptr) {
    std::fprintf(stderr, "[state dump]   DIA record %s: %zu others with members\n", dia_name(type).c_str(), others.size());
  }
  std::stable_sort(others.begin(), others.end(), [](auto& a, auto& b) { return a.first > b.first; });
  for (auto& o : others) out.push_back(o.second);
  return out;
}

bool dia_resolve_from(const std::string& s, size_t i, IDiaSymbol* type, std::vector<uintptr_t>& offsets,
                      std::string& error) {
  if (i == std::string::npos || i >= s.size()) return true;
  if (s[i] == '@') {
    size_t next = s.find_first_of(".[-@", i + 1);
    std::string name = s.substr(i + 1, next == std::string::npos ? std::string::npos : next - i - 1);
    std::vector<IDiaSymbol*> list;
    if (name.size() > 2 && name.front() == '(' && name.back() == ')') {
      for (IDiaSymbol* f : dia_children(sDiaGlobal, SymTagFunction, name.substr(1, name.size() - 2))) {
        auto args = dia_children(dia_type(f), SymTagFunctionArgType, "");
        if (!args.empty()) list.push_back(dia_type(dia_plain(dia_type(args[0]))));
        break;
      }
    } else {
      list = dia_children(sDiaGlobal, SymTagUDT, name);
    }
    if (list.empty()) {
      error = "type " + name + " not found";
      return false;
    }
    for (IDiaSymbol* t : list) {
      if (t != nullptr && dia_resolve_from(s, next, t, offsets, error)) return true;
    }
    return false;
  }
  if (s[i] == '[') {
    size_t close = s.find(']', i);
    uint32_t index = static_cast<uint32_t>(std::strtoul(s.c_str() + i + 1, nullptr, 0));
    IDiaSymbol* t = dia_plain(type);
    IDiaSymbol* elem = dia_tag(t) == SymTagArrayType ? dia_type(t) : nullptr;
    ULONGLONG length = 0;
    if (elem == nullptr || FAILED(elem->get_length(&length))) {
      error = "[" + std::to_string(index) + "] on something that isn't an array";
      return false;
    }
    offsets.back() += static_cast<uintptr_t>(index * length);
    if (dia_resolve_from(s, close == std::string::npos ? close : close + 1, elem, offsets, error)) return true;
    offsets.back() -= static_cast<uintptr_t>(index * length);
    return false;
  }
  bool arrow = s.compare(i, 2, "->") == 0;
  size_t from = i + (arrow ? 2 : 1);
  size_t next = s.find_first_of(".[-@", from);
  std::string name = s.substr(from, next == std::string::npos ? std::string::npos : next - from);
  if (arrow) {
    IDiaSymbol* t = dia_plain(type);
    if (dia_tag(t) != SymTagPointerType) {
      error = "->" + name + " on something that isn't a pointer";
      return false;
    }
    type = dia_type(t);
    offsets.push_back(0);
    if (!name.empty() && name[0] == '+') {
      offsets.back() += static_cast<uintptr_t>(std::strtoul(name.c_str() + 1, nullptr, 0));
      if (dia_resolve_from(s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
    if (name.empty()) {
      if (dia_resolve_from(s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
  }
  bool found = false;
  for (IDiaSymbol* candidate : dia_candidates(type)) {
    for (IDiaSymbol* m : dia_children(candidate, SymTagData, name)) {
      LONG offset = 0;
      if (FAILED(m->get_offset(&offset))) continue;
      found = true;
      offsets.back() += offset;
      if (dia_resolve_from(s, next, dia_type(m), offsets, error)) return true;
      offsets.back() -= offset;
    }
  }
  if (!found) error = "member " + name + " not found";
  if (arrow) offsets.pop_back();
  return false;
}

bool dia_resolve(Probe& p, const std::string& symbol, size_t i) {
  if (!dia_open()) return false;
  auto globals = dia_children(sDiaGlobal, SymTagData, symbol);
  if (globals.empty()) return false;
  std::string error = "not found";
  for (IDiaSymbol* g : globals) {
    ULONGLONG va = 0;
    if (g->get_virtualAddress(&va) != S_OK || va == 0) continue;
    std::vector<uintptr_t> offsets{static_cast<uintptr_t>(va)};
    if (dia_resolve_from(p.path, i, dia_type(g), offsets, error)) {
      p.offsets = offsets;
      std::fprintf(stderr, "[state dump] %s:", p.label.c_str());
      for (size_t k = 0; k < offsets.size(); k++) {
        std::fprintf(stderr, k == 0 ? " +0x%llx" : " -> +0x%llx",
                     static_cast<unsigned long long>(k == 0 ? offsets[0] - va : offsets[k]));
      }
      std::fprintf(stderr, "\n");
      return true;
    }
  }
  std::fprintf(stderr, "[state dump] %s: %s\n", p.label.c_str(), error.c_str());
  return true;
}
#endif

void resolve(Probe& p) {
  if (p.path == "@mem1") {
    p.type = "hash";
    p.count = p.count > 1 ? p.count : 96;
    p.size = kMem1Size / p.count;
    p.address = reinterpret_cast<const uint8_t*>(1);
    return;
  }
  if (p.path == "@data") {
    const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t lo = UINT32_MAX, hi = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
      if ((sec[i].Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
        lo = std::min<uint32_t>(lo, sec[i].VirtualAddress);
        hi = std::max<uint32_t>(hi, sec[i].VirtualAddress + sec[i].Misc.VirtualSize);
      }
    }
    if (lo < hi) {
      p.type = "hash";
      p.size = 0x10000;
      p.count = (hi - lo) / p.size;
      p.address = image + lo;
      std::fprintf(stderr, "[state dump] %s: writable sections at +0x%x..+0x%x, %u blocks of 64 KB\n",
                   p.label.c_str(), lo, hi, p.count);
      if (const char* rvas = std::getenv("XD_STATE_NAME_RVAS"); rvas != nullptr && rvas[0] != '\0') {
        HANDLE process = GetCurrentProcess();
        for (const char* q = rvas; *q != '\0';) {
          char* end = nullptr;
          const unsigned long rva = std::strtoul(q, &end, 16);
          if (end == q) break;
          alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
          auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
          sym->SizeOfStruct = sizeof(SYMBOL_INFO);
          sym->MaxNameLen = 255;
          DWORD64 offset = 0;
          const bool found = SymFromAddr(process, reinterpret_cast<DWORD64>(image) + rva, &offset, sym) != FALSE;
          std::fprintf(stderr, "[state dump] +0x%lx = %s+0x%llx (size 0x%lx)\n", rva, found ? sym->Name : "?",
                       static_cast<unsigned long long>(offset), found ? sym->Size : 0ul);
          q = *end == ',' ? end + 1 : end;
        }
      }
    }
    return;
  }
  HANDLE process = GetCurrentProcess();
  const std::string& s = p.path;
  size_t i = s.find_first_of(".[-@");
  std::string symbol = s.substr(0, i);
#ifdef XD_DIA_DLL
  if (dia_resolve(p, symbol, i)) return;
#endif
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
  std::memset(buffer, 0, sizeof(buffer));
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  if (!SymFromName(process, symbol.c_str(), sym)) {
    std::fprintf(stderr, "[state dump] %s: symbol %s not found\n", p.label.c_str(), symbol.c_str());
    return;
  }
  const ULONG64 base = sym->ModBase;
  std::vector<uintptr_t> offsets{static_cast<uintptr_t>(sym->Address)};
  std::string error;
  if (!resolve_from(process, base, s, i, sym->TypeIndex, offsets, error)) {
    std::fprintf(stderr, "[state dump] %s: %s\n", p.label.c_str(), error.c_str());
    return;
  }
  p.offsets = offsets;
  std::fprintf(stderr, "[state dump] %s:", p.label.c_str());
  for (size_t k = 0; k < offsets.size(); k++) {
    std::fprintf(stderr, k == 0 ? " +0x%llx" : " -> +0x%llx",
                 static_cast<unsigned long long>(k == 0 ? offsets[0] - sym->Address : offsets[k]));
  }
  std::fprintf(stderr, "\n");
}

bool safe_copy(void* to, uintptr_t from, size_t n) {
  __try {
    std::memcpy(to, reinterpret_cast<const void*>(from), n);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

const uint8_t* locate(const Probe& p) {
  if (p.offsets.empty()) return nullptr;
  uintptr_t a = p.offsets[0];
  for (size_t i = 1; i < p.offsets.size(); i++) {
    uintptr_t next = 0;
    if (!safe_copy(&next, a, sizeof(next)) || next == 0) return nullptr;
    a = next + p.offsets[i];
  }
  uint8_t probe = 0;
  if (!safe_copy(&probe, a, 1)) return nullptr;
  return reinterpret_cast<const uint8_t*>(a);
}

bool wanted(uint32_t frame) {
  if (sFrames.count(frame) != 0) return true;
  return sRangeStep != 0 && frame >= sRangeFrom && frame <= sRangeTo && (frame - sRangeFrom) % sRangeStep == 0;
}

void on_frame(uint32_t frame) {
  if (sSeedPending && frame >= sSeedFrame && __hsd_rand_seed_hold != nullptr) {
    *__hsd_rand_seed_hold = sSeedValue;
    sSeedPending = false;
    std::fprintf(stderr, "[state dump] game frame %u: random-number state set to 0x%08x (XD_RNG_SEED)\n", frame,
                 sSeedValue);
  }
  if (sProbes.empty() || !wanted(frame)) return;
  std::fprintf(sOut, "frame %u\n", frame);
  for (const Probe& p : sProbes) {
    std::fprintf(sOut, "%s", p.label.c_str());
    const uint8_t* address = p.path == "@mem1"   ? static_cast<const uint8_t*>(MEM1Start)
                             : p.path == "@data" ? p.address
                                                 : locate(p);
    if (address == nullptr) {
      std::fprintf(sOut, " ?\n");
      continue;
    }
    for (uint32_t i = 0; i < p.count; i++) {
      const uint8_t* a = address + i * p.size;
      if (!p.fields.empty()) {
        for (char c : p.fields) {
          uint32_t v = 0;
          uint32_t n = c == 'H' ? 2 : c == 'I' ? 4 : 1;
          std::memcpy(&v, a, n);
          a += n;
          if (c != 'x') std::fprintf(sOut, " %x", v);
        }
        continue;
      }
      uint32_t v = 0;
      if (p.type == "hash") v = fnv1a(a, p.size);
      else if (p.size == 1) v = *a;
      else if (p.size == 2) v = *reinterpret_cast<const uint16_t*>(a);
      else std::memcpy(&v, a, 4);
      std::fprintf(sOut, " %x", v);
    }
    std::fprintf(sOut, "\n");
    const char* raw = std::getenv("XD_STATE_RAW_DIR");
    if (p.type == "hash" && raw != nullptr && raw[0] != '\0') {
      const std::string path = std::string(raw) + "\\" + p.label + "_" + std::to_string(frame) + ".bin";
      if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(address, 1, static_cast<size_t>(p.size) * p.count, f);
        std::fclose(f);
      }
    }
  }
  std::fflush(sOut);
}

void parse_frames(const char* spec) {
  std::string s(spec);
  size_t dash = s.find('-');
  if (dash != std::string::npos && s.find(',') == std::string::npos) {
    sRangeFrom = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
    sRangeTo = static_cast<uint32_t>(std::strtoul(s.c_str() + dash + 1, nullptr, 10));
    size_t slash = s.find('/');
    sRangeStep = slash != std::string::npos ? static_cast<uint32_t>(std::strtoul(s.c_str() + slash + 1, nullptr, 10)) : 1;
    if (sRangeStep == 0) sRangeStep = 1;
    return;
  }
  size_t pos = 0;
  while (pos < s.size()) {
    sFrames.insert(static_cast<uint32_t>(std::strtoul(s.c_str() + pos, nullptr, 10)));
    size_t comma = s.find(',', pos);
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
}

}

extern "C" void PortXD_StateDumpStart(void) {
  const char* seed = std::getenv("XD_RNG_SEED");
  if (seed != nullptr && seed[0] != '\0') {
    sSeedValue = static_cast<uint32_t>(std::strtoul(seed, nullptr, 16));
    const char* at = std::strchr(seed, '@');
    sSeedFrame = at != nullptr ? static_cast<uint32_t>(std::strtoul(at + 1, nullptr, 10)) : 1;
    sSeedPending = true;
  }
  const char* probes = std::getenv("XD_STATE_PROBES");
  if (probes != nullptr && probes[0] != '\0') {
    FILE* f = std::fopen(probes, "r");
    if (f == nullptr) {
      std::fprintf(stderr, "[state dump] can't read %s\n", probes);
    } else {
      HANDLE process = GetCurrentProcess();
      SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
      SymInitialize(process, nullptr, TRUE);
      char line[2048];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        char label[128], path[512], type[512];
        if (line[0] == '#' || std::sscanf(line, "%127s %511s %511s", label, path, type) != 3) continue;
        Probe p;
        p.label = label;
        p.path = path;
        p.type = type;
        if (p.type.rfind("rec:", 0) == 0) p.type = "rec:" + expand_fields(p.type.substr(4));
        p.size = type_size(p.type);
        if (p.type.rfind("rec:", 0) == 0) p.fields = p.type.substr(4);
        char rest[256] = "";
        int n = 0;
        if (std::sscanf(line, "%*s %*s %*s %n", &n) >= 0 && n > 0) {
          std::strncpy(rest, line + n, sizeof(rest) - 1);
        }
        unsigned count = 0;
        if (std::sscanf(rest, "%u", &count) == 1 && count > 0) p.count = count;
        resolve(p);
        sProbes.push_back(p);
      }
      std::fclose(f);
      const char* out = std::getenv("XD_STATE_OUT");
      sOut = out != nullptr && out[0] != '\0' ? std::fopen(out, "w") : stdout;
      if (sOut == nullptr) sOut = stdout;
      const char* frames = std::getenv("XD_STATE_FRAMES");
      parse_frames(frames != nullptr ? frames : "");
      std::fprintf(stderr, "[state dump] %zu probes from %s\n", sProbes.size(), probes);
    }
  }
  if (!sProbes.empty() || sSeedPending) {
    PortInput_SetFrameHook(on_frame);
  }
}

extern "C" const void* PortXD_FindVariable(const char* path) {
  static bool initialised = false;
  if (!initialised) {
    initialised = true;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
  }
  Probe p;
  p.label = path;
  p.path = path;
  p.type = "u8";
  p.size = 1;
  resolve(p);
  return locate(p);
}
