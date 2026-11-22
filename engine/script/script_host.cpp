#include "script_host.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace engine::script {

namespace {

#define LERR(L, ...)            \
  do {                          \
    luaL_error(L, __VA_ARGS__); \
    return 0;                   \
  } while (0)

constexpr int kMaxHandlersPerHook = 32;
constexpr size_t kMaxLogLine = 200;
constexpr const char* kHandlersKey = "port_script_handlers";
constexpr const char* kFunctionsKey = "port_script_functions";
constexpr int kMaxStoryFunctions = 64;

struct StateUd {
  size_t used = 0;
  size_t limit = 0;
  uint32_t steps = 0;
  uint32_t stepsGiven = 0;
  bool frameLimited = false;
  bool cutByFrame = false;
  uint32_t logThisCall = 0;
  uint32_t logTotal = 0;
  uint32_t logPerCall = 0;
  uint32_t logTotalMax = 0;
  std::string modId;
  const std::set<std::string>* hooks = nullptr;
  const std::vector<std::pair<std::string, Value>>* constants = nullptr;
  const std::vector<std::pair<std::string, ApiFunction>>* api = nullptr;
  const Logger* logger = nullptr;
  int storyFunctions = 0;
  std::map<std::string, int> handlerCount;
};

StateUd* UdOf(lua_State* L) { return static_cast<StateUd*>(lua_callbacks(L)->userdata); }

void* Alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  StateUd* s = static_cast<StateUd*>(ud);
  if (nsize == 0) {
    if (ptr != nullptr) {
      s->used -= osize;
      std::free(ptr);
    }
    return nullptr;
  }
  const size_t old = ptr != nullptr ? osize : 0;
  if (nsize > old && s->used + (nsize - old) > s->limit) {
    return nullptr;
  }
  void* p = std::realloc(ptr, nsize);
  if (p == nullptr) {
    return nullptr;
  }
  s->used = s->used - old + nsize;
  return p;
}

void Interrupt(lua_State* L, int gc) {
  if (gc >= 0) return;
  StateUd* s = UdOf(L);
  if (s->steps > 0) {
    s->steps--;
  }
  if (s->steps == 0 && s->frameLimited) {
    s->cutByFrame = true;
    lua_pushstring(L, "cut: all scripts together used up their steps for this game frame");
    lua_error(L);
  }
  if (s->steps == 0) {
    lua_Debug ar;
    const char* where = "";
    int line = 0;
    if (lua_getinfo(L, 0, "sl", &ar) != 0 && ar.currentline > 0) {
      where = ar.short_src;
      line = ar.currentline;
    }
    if (line > 0) {
      lua_pushfstring(L, "%s:%d: ran too long (more than %d steps in one call: a step is a function call or a loop turn)", where, line,
                      static_cast<int>(s->stepsGiven));
    } else {
      lua_pushfstring(L, "ran too long (more than %d steps in one call: a step is a function call or a loop turn)", static_cast<int>(s->stepsGiven));
    }
    lua_error(L);
  }
}

bool Allowed(const char* name, const char* const* list) {
  for (; *list != nullptr; list++) {
    if (std::strcmp(name, *list) == 0) return true;
  }
  return false;
}

void Prune(lua_State* L, const char* const* allowed) {
  std::vector<std::string> drop;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    if (lua_type(L, -1) == LUA_TSTRING) {
      if (!Allowed(lua_tostring(L, -1), allowed)) drop.emplace_back(lua_tostring(L, -1));
    }
  }
  for (const std::string& name : drop) {
    lua_pushnil(L);
    lua_setfield(L, -2, name.c_str());
  }
}

std::string PlainText(lua_State* L, int index) {
  switch (lua_type(L, index)) {
  case LUA_TNIL:
  case LUA_TBOOLEAN:
  case LUA_TNUMBER:
  case LUA_TSTRING: {
    size_t n = 0;
    const char* s = luaL_tolstring(L, index, &n);
    std::string out(s, n);
    lua_pop(L, 1);
    return out;
  }
  default:
    return std::string("<") + luaL_typename(L, index) + ">";
  }
}

int SafeToString(lua_State* L) {
  luaL_checkany(L, 1);
  const std::string text = PlainText(L, 1);
  lua_pushlstring(L, text.data(), text.size());
  return 1;
}

int LuaLog(lua_State* L) {
  StateUd* s = UdOf(L);
  std::string line;
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (i > 1) line += ' ';
    line += PlainText(L, i);
    if (line.size() > kMaxLogLine) {
      line.resize(kMaxLogLine);
      line += "...";
      break;
    }
  }
  if (s->logThisCall < s->logPerCall && s->logTotal < s->logTotalMax && s->logger != nullptr && *s->logger) {
    s->logThisCall++;
    s->logTotal++;
    (*s->logger)("[script " + s->modId + "] " + line);
  }
  return 0;
}

int SafeFormat(lua_State* L) {
  size_t n = 0;
  const char* f = luaL_checklstring(L, 1, &n);
  for (size_t i = 0; i < n; i++) {
    if (f[i] != '%') continue;
    i++;
    while (i < n && f[i] != 0 && std::strchr("-+ #0123456789.", f[i]) != nullptr) i++;
    if (i < n && (f[i] == 'p' || f[i] == 'a' || f[i] == 'A')) {
      LERR(L, "string.format: '%%%c' is not available to mod scripts", f[i]);
    }
  }
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

int LuaOn(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  StateUd* s = UdOf(L);
  if (s->hooks->count(name) == 0) {
    std::string known;
    for (const std::string& h : *s->hooks) known += (known.empty() ? "" : ", ") + h;
    LERR(L, "there is no hook called \"%s\" (hooks: %s)", name, known.c_str());
  }
  if (s->handlerCount[name] >= kMaxHandlersPerHook) {
    LERR(L, "too many handlers for \"%s\" (at most %d)", name, kMaxHandlersPerHook);
  }
  lua_getfield(L, LUA_REGISTRYINDEX, kHandlersKey);
  lua_getfield(L, -1, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, lua_objlen(L, -2) + 1);
  s->handlerCount[name]++;
  return 0;
}

void PushValue(lua_State* L, const Value& v, int depth = 0) {
  luaL_checkstack(L, 6, "event too deep");
  switch (v.kind) {
  case Value::Kind::Nil: lua_pushnil(L); break;
  case Value::Kind::Bool: lua_pushboolean(L, v.boolean ? 1 : 0); break;
  case Value::Kind::Int: lua_pushnumber(L, static_cast<double>(v.integer)); break;
  case Value::Kind::String: lua_pushlstring(L, v.text.data(), v.text.size()); break;
  case Value::Kind::List:
    lua_createtable(L, static_cast<int>(v.fieldValues.size()), 0);
    if (depth < 6) {
      for (size_t i = 0; i < v.fieldValues.size(); i++) {
        PushValue(L, v.fieldValues[i], depth + 1);
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
      }
    }
    break;
  case Value::Kind::Table:
    lua_createtable(L, 0, static_cast<int>(v.fieldNames.size()));
    if (depth < 6) {
      for (size_t i = 0; i < v.fieldNames.size(); i++) {
        PushValue(L, v.fieldValues[i], depth + 1);
        lua_setfield(L, -2, v.fieldNames[i].c_str());
      }
    }
    break;
  }
}

int ConstIndex(lua_State* L) {
  if (lua_type(L, 2) == LUA_TSTRING) {
    LERR(L, "%s has no name \"%s\" (find(\"category\", \"name\") returns nil instead of failing, to test for one)",
                      lua_tostring(L, lua_upvalueindex(1)), lua_tostring(L, 2));
  }
  return 0;
}

int ConstNewIndex(lua_State* L) {
  LERR(L, "%s is read-only", lua_tostring(L, lua_upvalueindex(1)));
}

void PushConstant(lua_State* L, const std::string& name, const Value& v) {
  PushValue(L, v);
  if (v.kind != Value::Kind::Table) return;
  lua_createtable(L, 0, 4);
  lua_pushstring(L, name.c_str());
  lua_pushcclosure(L, ConstIndex, "index", 1);
  lua_setfield(L, -2, "__index");
  lua_pushstring(L, name.c_str());
  lua_pushcclosure(L, ConstNewIndex, "newindex", 1);
  lua_setfield(L, -2, "__newindex");
  lua_pushstring(L, "read-only");
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);
  lua_setreadonly(L, -1, 1);
}

[[noreturn]] void ArgError(lua_State* L, const char* message) {
  luaL_error(L, "%s", message);
  std::abort();
}

Value ReadArg(lua_State* L, int index, int depth, int& budget) {
  switch (lua_type(L, index)) {
  case LUA_TNUMBER: return Value::Int(static_cast<int64_t>(std::trunc(lua_tonumber(L, index))));
  case LUA_TSTRING: {
    size_t len = 0;
    const char* t = lua_tolstring(L, index, &len);
    return Value::Text(std::string(t, len));
  }
  case LUA_TBOOLEAN: return Value::Bool(lua_toboolean(L, index) != 0);
  case LUA_TNIL: return Value();
  case LUA_TTABLE: {
    if (depth >= 4) ArgError(L, "a table handed to a game function may be at most 4 levels deep");
    luaL_checkstack(L, 6, "table too deep");
    const int n = static_cast<int>(lua_objlen(L, index));
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
      count++;
      if (--budget < 0) ArgError(L, "a table handed to a game function is too big (at most 400 entries)");
      lua_pop(L, 1);
    }
    if (n > 0 && count == n) {
      Value list = Value::List();
      for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, index, i);
        list.Add(ReadArg(L, lua_gettop(L), depth + 1, budget));
        lua_pop(L, 1);
      }
      return list;
    }
    Value table = Value::Table();
    std::vector<std::pair<std::string, Value>> fields;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
      if (lua_type(L, -2) != LUA_TSTRING) ArgError(L, "a table handed to a game function has either the keys 1, 2, 3... or names, not a mix");
      const std::string key = lua_tostring(L, -2);
      fields.emplace_back(key, ReadArg(L, lua_gettop(L), depth + 1, budget));
      lua_pop(L, 1);
    }
    std::sort(fields.begin(), fields.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& f : fields) table.Set(f.first, std::move(f.second));
    return table;
  }
  default: luaL_argerror(L, index, "a number, text, true/false, nil or a table of those is expected");
  }
  return Value();
}

int ApiThunk(lua_State* L) {
  StateUd* s = UdOf(L);
  const size_t which = static_cast<size_t>(lua_tonumber(L, lua_upvalueindex(1)));
  if (s->api == nullptr || which >= s->api->size()) LERR(L, "internal error: unknown game function");
  const int n = lua_gettop(L);
  std::vector<Value> args;
  int budget = 400;
  for (int i = 1; i <= n; i++) args.push_back(ReadArg(L, i, 0, budget));
  std::string error;
  const Value out = (*s->api)[which].second(s->modId, args, error);
  if (!error.empty()) LERR(L, "%s", error.c_str());
  PushValue(L, out);
  return 1;
}

int LuaStoryFunction(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  StateUd* s = UdOf(L);
  if (std::strlen(name) == 0 || std::strlen(name) > 60 || std::strchr(name, ':') != nullptr) {
    LERR(L, "a story function's name is 1 to 60 characters and has no ':' in it");
  }
  if (s->storyFunctions >= kMaxStoryFunctions) LERR(L, "too many story functions (at most %d)", kMaxStoryFunctions);
  lua_getfield(L, LUA_REGISTRYINDEX, kFunctionsKey);
  lua_getfield(L, -1, name);
  if (!lua_isnil(L, -1)) LERR(L, "the story function \"%s\" is registered twice", name);
  lua_pop(L, 1);
  lua_pushvalue(L, 2);
  lua_setfield(L, -2, name);
  s->storyFunctions++;
  return 0;
}

int Setup(lua_State* L) {
  static const char* const kGlobals[] = {"assert", "error", "getmetatable", "ipairs", "next", "pairs", "rawequal", "rawget",
                                         "rawlen", "rawset", "select", "setmetatable", "tonumber", "tostring", "type", "typeof",
                                         "unpack", "_G", "_VERSION", "string", "table", "math", "bit32", nullptr};
  static const char* const kString[] = {"byte", "char", "format", "len", "lower", "rep", "reverse", "sub", "upper", nullptr};
  static const char* const kTable[] = {"clear", "clone", "concat", "find", "insert", "move", "pack", "remove", "sort", "unpack", nullptr};
  static const char* const kMath[] = {"abs", "ceil", "clamp", "floor", "fmod", "huge", "max", "min", "modf", "pi", "round", "sign", "sqrt", nullptr};
  luaL_openlibs(L);
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  Prune(L, kGlobals);
  lua_getfield(L, -1, "string");
  Prune(L, kString);
  lua_getfield(L, -1, "format");
  lua_pushcclosure(L, SafeFormat, "format", 1);
  lua_setfield(L, -2, "format");
  lua_pop(L, 1);
  lua_getfield(L, -1, "table");
  Prune(L, kTable);
  lua_pop(L, 1);
  lua_getfield(L, -1, "math");
  Prune(L, kMath);
  lua_pop(L, 1);

  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kHandlersKey);
  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kFunctionsKey);

  lua_pushcfunction(L, SafeToString, "tostring");
  lua_setfield(L, -2, "tostring");
  lua_pushcfunction(L, LuaLog, "log");
  lua_setfield(L, -2, "log");
  lua_pushcfunction(L, LuaLog, "print");
  lua_setfield(L, -2, "print");
  lua_pushcfunction(L, LuaOn, "on");
  lua_setfield(L, -2, "on");
  lua_pushcfunction(L, LuaStoryFunction, "story_function");
  lua_setfield(L, -2, "story_function");
  lua_pushnumber(L, 1);
  lua_setfield(L, -2, "API_VERSION");
  lua_pushstring(L, "Luau");
  lua_setfield(L, -2, "SCRIPT_LANGUAGE");
  const StateUd* s = UdOf(L);
  if (s->api != nullptr) {
    for (size_t i = 0; i < s->api->size(); i++) {
      lua_pushnumber(L, static_cast<double>(i));
      lua_pushcclosure(L, ApiThunk, (*s->api)[i].first.c_str(), 1);
      lua_setfield(L, -2, (*s->api)[i].first.c_str());
    }
  }
  if (s->constants != nullptr) {
    for (const auto& [name, value] : *s->constants) {
      PushConstant(L, name, value);
      lua_setfield(L, -2, name.c_str());
    }
  }
  lua_pop(L, 1);
  luaL_sandbox(L);
  return 0;
}

std::string ErrorText(lua_State* L, int rc, const StateUd& ud) {
  std::string text;
  if (lua_type(L, -1) == LUA_TSTRING) {
    text = lua_tostring(L, -1);
  } else {
    text = std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
  }
  if (rc == LUA_ERRMEM) {
    text = "not enough memory (a mod's scripts may use " + std::to_string(ud.limit >> 20) + " MB)";
  }
  return text;
}

uint64_t Fnv(uint64_t h, const std::string& s) {
  for (unsigned char c : s) {
    h = (h ^ c) * 1099511628211ull;
  }
  return (h ^ 0xFF) * 1099511628211ull;
}

bool Compile(const std::string& chunk, const std::string& text, std::string& bytecode, std::string& error) {
  if (!text.empty() && (static_cast<unsigned char>(text[0]) < 0x09 || text[0] == '\x1b')) {
    error = chunk + ": precompiled (binary) code is not accepted; a script is plain text";
    return false;
  }
  lua_CompileOptions options = {};
  options.optimizationLevel = 1;
  options.debugLevel = 1;
  size_t size = 0;
  char* out = luau_compile(text.data(), text.size(), &options, &size);
  if (out == nullptr) {
    error = chunk + ": the script could not be compiled (out of memory)";
    return false;
  }
  const bool failed = size > 0 && out[0] == 0;
  if (failed) {
    error = chunk + std::string(out + 1, size - 1);
  } else {
    bytecode.assign(out, size);
  }
  std::free(out);
  return !failed;
}

}

struct Host::Impl {
  struct Mod {
    std::string id;
    std::unique_ptr<StateUd> ud;
    lua_State* L = nullptr;
    lua_State* T = nullptr;
    bool alive = false;
  };

  std::set<std::string> hooks;
  Limits limits;
  Logger logger;
  std::vector<std::pair<std::string, Value>> constants;
  std::vector<std::pair<std::string, ApiFunction>> api;
  std::string prelude;
  std::vector<std::unique_ptr<Mod>> mods;
  std::vector<Failure> failures;
  uint64_t digest = 1469598103934665603ull;

  void Say(const std::string& line) {
    if (logger) logger(line);
  }

  void Fail(Mod& m, const std::string& message) {
    if (m.L != nullptr) {
      lua_close(m.L);
      m.L = nullptr;
      m.T = nullptr;
    }
    m.alive = false;
    failures.push_back({m.id, message});
    Say("[script " + m.id + "] switched off: " + message);
  }

  void Arm(Mod& m, uint32_t steps) {
    m.ud->stepsGiven = m.ud->steps = steps;
    m.ud->logThisCall = 0;
    m.ud->cutByFrame = false;
  }

  std::function<uint64_t()> clock;
  uint64_t frame = ~0ull;
  uint32_t frameLeft = 0;
  bool frameSaid = false;
  BudgetStats stats;

  uint32_t GiveSteps(Mod& m, uint32_t perCall) {
    m.ud->frameLimited = false;
    if (limits.frameInstructions == 0) return perCall;
    if (clock) {
      const uint64_t f = clock();
      if (f != frame) {
        frame = f;
        frameLeft = limits.frameInstructions;
        frameSaid = false;
      }
    }
    if (frame == ~0ull) {
      frame = 0;
      frameLeft = limits.frameInstructions;
    }
    if (frameLeft == 0) {
      stats.skipped++;
      if (!frameSaid) {
        frameSaid = true;
        Say("[script] the scripts used up their " + std::to_string(limits.frameInstructions) + " steps for this game frame: the rest wait for the next frame");
      }
      return 0;
    }
    m.ud->frameLimited = frameLeft < perCall;
    return frameLeft < perCall ? frameLeft : perCall;
  }
  void Spend(Mod& m) {
    if (limits.frameInstructions == 0) return;
    const uint32_t used = m.ud->stepsGiven >= m.ud->steps ? m.ud->stepsGiven - m.ud->steps : 0;
    frameLeft = used >= frameLeft ? 0 : frameLeft - used;
    if (m.ud->cutByFrame) {
      stats.cut++;
      Say("[script " + m.id + "] a handler was cut: the scripts of all mods together used up their " + std::to_string(limits.frameInstructions) +
          " steps for this game frame (the handler's answer is not used; the mod stays on)");
    }
  }

  bool RunChunk(Mod& m, const std::string& name, const std::string& text, std::string& error) {
    std::string bytecode;
    if (!Compile(name, text, bytecode, error)) return false;
    lua_State* T = m.T;
    const int top = lua_gettop(T);
    const std::string chunk = "@" + name;
    int rc = luau_load(T, chunk.c_str(), bytecode.data(), bytecode.size(), 0);
    if (rc == 0) {
      Arm(m, limits.loadInstructions);
      m.ud->frameLimited = false;
      rc = lua_pcall(T, 0, 0, 0);
    }
    if (rc != 0) {
      error = ErrorText(T, rc, *m.ud);
      lua_settop(T, top);
      return false;
    }
    lua_settop(T, top);
    return true;
  }
};

Host::Host(std::vector<std::string> hooks, Limits limits, Logger logger) : mImpl(new Impl) {
  mImpl->hooks.insert(hooks.begin(), hooks.end());
  mImpl->limits = limits;
  mImpl->logger = std::move(logger);
}

Host::~Host() {
  for (auto& m : mImpl->mods) {
    if (m->L != nullptr) lua_close(m->L);
  }
}

void Host::SetConstants(const std::string& name, const Value& table) { mImpl->constants.emplace_back(name, table); }

void Host::SetPrelude(const std::string& source) { mImpl->prelude = source; }

void Host::SetApi(const std::string& name, ApiFunction function) { mImpl->api.emplace_back(name, std::move(function)); }

bool Host::AddMod(const std::string& modId, const std::vector<Source>& sources, std::string& error) {
  Impl& d = *mImpl;
  d.digest = Fnv(d.digest, modId);
  for (const Source& s : sources) {
    d.digest = Fnv(Fnv(d.digest, s.name), s.text);
  }
  auto mod = std::make_unique<Impl::Mod>();
  Impl::Mod& m = *mod;
  m.id = modId;
  m.ud = std::make_unique<StateUd>();
  StateUd& ud = *m.ud;
  ud.limit = d.limits.memoryBytes;
  ud.modId = modId;
  ud.hooks = &d.hooks;
  ud.constants = &d.constants;
  ud.api = &d.api;
  ud.logger = &d.logger;
  ud.logPerCall = d.limits.logLinesPerCall;
  ud.logTotalMax = d.limits.logLinesTotal;
  d.mods.push_back(std::move(mod));

  m.L = lua_newstate(Alloc, &ud);
  if (m.L == nullptr) {
    error = "the scripts' memory could not be set up";
    d.Fail(m, error);
    return false;
  }
  lua_State* L = m.L;
  lua_callbacks(L)->userdata = &ud;
  lua_callbacks(L)->interrupt = Interrupt;
  d.Arm(m, d.limits.loadInstructions);
  m.ud->frameLimited = false;
  lua_pushcfunction(L, Setup, "setup");
  int rc = lua_pcall(L, 0, 0, 0);
  if (rc != 0) {
    error = "the script sandbox could not be set up: " + ErrorText(L, rc, ud);
    d.Fail(m, error);
    return false;
  }
  m.T = lua_newthread(L);
  lua_ref(L, -1);
  lua_pop(L, 1);
  luaL_sandboxthread(m.T);
  lua_pushvalue(m.T, LUA_GLOBALSINDEX);
  lua_setfield(m.T, LUA_GLOBALSINDEX, "_G");
  m.alive = true;
  if (!d.prelude.empty()) {
    std::string why;
    if (!d.RunChunk(m, "game helpers", d.prelude, why)) {
      error = "the game's script helpers failed: " + why;
      d.Fail(m, error);
      return false;
    }
  }
  for (const Source& s : sources) {
    std::string why;
    if (!d.RunChunk(m, modId + "/" + s.name, s.text, why)) {
      error = why;
      d.Fail(m, error);
      return false;
    }
  }
  return true;
}

bool CheckSyntax(const Source& source, std::string& error) {
  std::string bytecode;
  return Compile(source.name, source.text, bytecode, error);
}

bool Host::HasHandlers(const std::string& hook) const {
  for (const auto& m : mImpl->mods) {
    if (!m->alive) continue;
    const auto it = m->ud->handlerCount.find(hook);
    if (it != m->ud->handlerCount.end() && it->second > 0) return true;
  }
  return false;
}

int64_t Host::CallInt(const std::string& hook, const Value& event, const std::string& resultKey, int64_t current) {
  Impl& d = *mImpl;
  int64_t value = current;
  for (auto& mp : d.mods) {
    Impl::Mod& m = *mp;
    if (!m.alive) continue;
    const auto it = m.ud->handlerCount.find(hook);
    if (it == m.ud->handlerCount.end() || it->second == 0) continue;
    lua_State* T = m.T;
    const int top = lua_gettop(T);
    lua_getfield(T, LUA_REGISTRYINDEX, kHandlersKey);
    lua_getfield(T, -1, hook.c_str());
    const int count = lua_objlen(T, -1);
    std::string problem;
    for (int i = 1; i <= count && problem.empty(); i++) {
      Value ev = event;
      bool replaced = false;
      for (size_t f = 0; f < ev.fieldNames.size(); f++) {
        if (ev.fieldNames[f] == resultKey) {
          ev.fieldValues[f] = Value::Int(value);
          replaced = true;
        }
      }
      if (!replaced) ev.Set(resultKey, Value::Int(value));
      const uint32_t give = d.GiveSteps(m, d.limits.callInstructions);
      if (give == 0) break;
      lua_rawgeti(T, -1, i);
      PushValue(T, ev);
      d.Arm(m, give);
      const int rc = lua_pcall(T, 1, 1, 0);
      d.Spend(m);
      if (rc != 0 && m.ud->cutByFrame) {
        lua_pop(T, 1);
        break;
      }
      if (rc != 0) {
        problem = "in the \"" + hook + "\" handler: " + ErrorText(T, rc, *m.ud);
        break;
      }
      if (lua_type(T, -1) == LUA_TNUMBER) {
        const double n = lua_tonumber(T, -1);
        if (!std::isfinite(n) || std::fabs(n) > 9e15) {
          problem = "the \"" + hook + "\" handler returned a number that is not usable";
          break;
        }
        value = static_cast<int64_t>(std::trunc(n));
      } else if (!lua_isnil(T, -1)) {
        problem = "the \"" + hook + "\" handler must return a number or nothing (it returned a " + luaL_typename(T, -1) + ")";
        break;
      }
      lua_pop(T, 1);
    }
    if (!problem.empty()) {
      d.Fail(m, problem);
    } else {
      lua_settop(T, top);
    }
  }
  return value;
}

namespace {
void SplitFunctionName(const std::string& full, std::string& mod, std::string& name) {
  const size_t colon = full.find(':');
  mod = colon == std::string::npos ? std::string() : full.substr(0, colon);
  name = colon == std::string::npos ? full : full.substr(colon + 1);
}
}

bool Host::HasFunction(const std::string& full) const {
  std::string wantMod, name;
  SplitFunctionName(full, wantMod, name);
  for (const auto& m : mImpl->mods) {
    if (!m->alive || (!wantMod.empty() && m->id != wantMod)) continue;
    lua_State* T = m->T;
    lua_getfield(T, LUA_REGISTRYINDEX, kFunctionsKey);
    lua_getfield(T, -1, name.c_str());
    const bool have = lua_isfunction(T, -1);
    lua_pop(T, 2);
    if (have) return true;
  }
  return false;
}

bool Host::CallFunction(const std::string& full, const std::vector<Value>& args, int64_t& result, std::string& error) {
  Impl& d = *mImpl;
  result = 0;
  std::string wantMod, name;
  SplitFunctionName(full, wantMod, name);
  for (auto& mp : d.mods) {
    Impl::Mod& m = *mp;
    if (!m.alive || (!wantMod.empty() && m.id != wantMod)) continue;
    lua_State* T = m.T;
    const int top = lua_gettop(T);
    lua_getfield(T, LUA_REGISTRYINDEX, kFunctionsKey);
    lua_getfield(T, -1, name.c_str());
    if (!lua_isfunction(T, -1)) {
      lua_settop(T, top);
      continue;
    }
    const uint32_t give = d.GiveSteps(m, d.limits.callInstructions);
    if (give == 0) {
      lua_settop(T, top);
      error = "the scripts' steps for this game frame are used up";
      return false;
    }
    for (const Value& a : args) PushValue(T, a);
    d.Arm(m, give);
    const int rc = lua_pcall(T, static_cast<int>(args.size()), 1, 0);
    d.Spend(m);
    std::string problem;
    if (rc != 0 && m.ud->cutByFrame) {
      lua_settop(T, top);
      error = "the story function \"" + name + "\" was cut: all scripts together used up their steps for this game frame";
      return false;
    }
    if (rc != 0) {
      problem = "in the story function \"" + name + "\": " + ErrorText(T, rc, *m.ud);
    } else if (lua_type(T, -1) == LUA_TNUMBER) {
      const double n = lua_tonumber(T, -1);
      if (!std::isfinite(n) || std::fabs(n) > 9e15) {
        problem = "the story function \"" + name + "\" returned a number that is not usable";
      } else {
        result = static_cast<int64_t>(std::trunc(n));
      }
    } else if (lua_isboolean(T, -1)) {
      result = lua_toboolean(T, -1) ? 1 : 0;
    } else if (!lua_isnil(T, -1)) {
      problem = "the story function \"" + name + "\" must return a number, true/false or nothing (it returned a " + luaL_typename(T, -1) + ")";
    }
    if (!problem.empty()) {
      result = 0;
      error = problem;
      d.Fail(m, problem);
      return false;
    }
    lua_settop(T, top);
    return true;
  }
  error = "no story function called \"" + full + "\" in the mods' scripts";
  return false;
}

void Host::SetFrameClock(std::function<uint64_t()> clock) { mImpl->clock = std::move(clock); }
Host::BudgetStats Host::Budget() const { return mImpl->stats; }

std::vector<Failure> Host::TakeFailures() {
  std::vector<Failure> out;
  out.swap(mImpl->failures);
  return out;
}

size_t Host::LiveMods() const {
  size_t n = 0;
  for (const auto& m : mImpl->mods) n += m->alive ? 1 : 0;
  return n;
}

std::vector<std::string> Host::LiveModIds() const {
  std::vector<std::string> out;
  for (const auto& m : mImpl->mods) {
    if (m->alive) out.push_back(m->id);
  }
  return out;
}

uint64_t Host::Digest() const { return mImpl->digest; }

}
