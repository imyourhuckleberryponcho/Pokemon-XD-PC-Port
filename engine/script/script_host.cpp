#include "script_host.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace engine::script {

namespace {

constexpr int kStepSize = 1000;
constexpr int kMaxHandlersPerHook = 32;
constexpr size_t kMaxLogLine = 200;
constexpr const char* kHandlersKey = "port_script_handlers";

struct StateUd {
  size_t used = 0;
  size_t limit = 0;
  uint32_t steps = 0;
  uint32_t stepsGiven = 0;
  uint32_t logThisCall = 0;
  uint32_t logTotal = 0;
  uint32_t logPerCall = 0;
  uint32_t logTotalMax = 0;
  std::string modId;
  const std::set<std::string>* hooks = nullptr;
  const std::vector<std::pair<std::string, Value>>* constants = nullptr;
  const Logger* logger = nullptr;
  std::map<std::string, int> handlerCount;
};

StateUd* UdOf(lua_State* L) {
  void* ud = nullptr;
  lua_getallocf(L, &ud);
  return static_cast<StateUd*>(ud);
}

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

void CountHook(lua_State* L, lua_Debug*) {
  StateUd* s = UdOf(L);
  if (s->steps > 0) {
    s->steps--;
  }
  if (s->steps == 0) {
    luaL_error(L, "ran too long (more than %d instructions in one call)", static_cast<int>(s->stepsGiven * kStepSize));
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
      return luaL_error(L, "string.format: '%%%c' is not available to mod scripts", f[i]);
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
    return luaL_error(L, "there is no hook called \"%s\" (hooks: %s)", name, known.c_str());
  }
  if (s->handlerCount[name] >= kMaxHandlersPerHook) {
    return luaL_error(L, "too many handlers for \"%s\" (at most %d)", name, kMaxHandlersPerHook);
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
  lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
  s->handlerCount[name]++;
  return 0;
}

void PushValue(lua_State* L, const Value& v, int depth = 0) {
  luaL_checkstack(L, 6, "event too deep");
  switch (v.kind) {
  case Value::Kind::Nil: lua_pushnil(L); break;
  case Value::Kind::Bool: lua_pushboolean(L, v.boolean ? 1 : 0); break;
  case Value::Kind::Int: lua_pushinteger(L, static_cast<lua_Integer>(v.integer)); break;
  case Value::Kind::String: lua_pushlstring(L, v.text.data(), v.text.size()); break;
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

int Setup(lua_State* L) {
  static const char* const kGlobals[] = {"assert", "error", "getmetatable", "ipairs", "next", "pairs", "rawequal", "rawget",
                                         "rawlen", "rawset", "select", "setmetatable", "tonumber", "tostring", "type",
                                         "_G", "_VERSION", "string", "table", "math", nullptr};
  static const char* const kString[] = {"byte", "char", "format", "len", "lower", "rep", "reverse", "sub", "upper", nullptr};
  static const char* const kTable[] = {"concat", "insert", "move", "pack", "remove", "sort", "unpack", nullptr};
  static const char* const kMath[] = {"abs", "ceil", "floor", "fmod", "huge", "max", "maxinteger", "min", "mininteger",
                                      "modf", "pi", "sqrt", "tointeger", "type", "ult", nullptr};
  luaL_requiref(L, "_G", luaopen_base, 1);
  Prune(L, kGlobals);
  lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1);
  Prune(L, kString);
  lua_getfield(L, -1, "format");
  lua_pushcclosure(L, SafeFormat, 1);
  lua_setfield(L, -2, "format");
  lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1);
  Prune(L, kTable);
  lua_pop(L, 1);
  luaL_requiref(L, "math", luaopen_math, 1);
  Prune(L, kMath);
  lua_pop(L, 1);

  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kHandlersKey);

  lua_pushglobaltable(L);
  lua_pushcfunction(L, SafeToString);
  lua_setfield(L, -2, "tostring");
  lua_pushcfunction(L, LuaLog);
  lua_setfield(L, -2, "log");
  lua_pushcfunction(L, LuaLog);
  lua_setfield(L, -2, "print");
  lua_pushcfunction(L, LuaOn);
  lua_setfield(L, -2, "on");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "API_VERSION");
  const StateUd* s = UdOf(L);
  if (s->constants != nullptr) {
    for (const auto& [name, value] : *s->constants) {
      PushValue(L, value);
      lua_setfield(L, -2, name.c_str());
    }
  }
  lua_pop(L, 1);
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

}

struct Host::Impl {
  struct Mod {
    std::string id;
    std::unique_ptr<StateUd> ud;
    lua_State* L = nullptr;
    bool alive = false;
  };

  std::set<std::string> hooks;
  Limits limits;
  Logger logger;
  std::vector<std::pair<std::string, Value>> constants;
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
    }
    m.alive = false;
    failures.push_back({m.id, message});
    Say("[script " + m.id + "] switched off: " + message);
  }

  void Arm(Mod& m, uint32_t instructions) {
    m.ud->stepsGiven = m.ud->steps = (instructions + kStepSize - 1) / kStepSize;
    m.ud->logThisCall = 0;
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
  lua_sethook(L, CountHook, LUA_MASKCOUNT, kStepSize);
  d.Arm(m, d.limits.loadInstructions);
  lua_pushcfunction(L, Setup);
  int rc = lua_pcall(L, 0, 0, 0);
  if (rc != LUA_OK) {
    error = "the script sandbox could not be set up: " + ErrorText(L, rc, ud);
    d.Fail(m, error);
    return false;
  }
  m.alive = true;
  for (const Source& s : sources) {
    const std::string chunk = "@" + modId + "/" + s.name;
    rc = luaL_loadbufferx(L, s.text.data(), s.text.size(), chunk.c_str(), "t");
    if (rc == LUA_OK) {
      d.Arm(m, d.limits.loadInstructions);
      rc = lua_pcall(L, 0, 0, 0);
    }
    if (rc != LUA_OK) {
      error = ErrorText(L, rc, ud);
      d.Fail(m, error);
      return false;
    }
  }
  return true;
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
    lua_State* L = m.L;
    const int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, kHandlersKey);
    lua_getfield(L, -1, hook.c_str());
    const int count = static_cast<int>(lua_rawlen(L, -1));
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
      lua_rawgeti(L, -1, i);
      PushValue(L, ev);
      d.Arm(m, d.limits.callInstructions);
      const int rc = lua_pcall(L, 1, 1, 0);
      if (rc != LUA_OK) {
        problem = "in the \"" + hook + "\" handler: " + ErrorText(L, rc, *m.ud);
        break;
      }
      if (lua_isinteger(L, -1)) {
        value = static_cast<int64_t>(lua_tointeger(L, -1));
      } else if (lua_isnumber(L, -1) && lua_type(L, -1) == LUA_TNUMBER) {
        const double n = lua_tonumber(L, -1);
        if (!std::isfinite(n) || std::fabs(n) > 9e15) {
          problem = "the \"" + hook + "\" handler returned a number that is not usable";
          break;
        }
        value = static_cast<int64_t>(std::trunc(n));
      } else if (!lua_isnil(L, -1)) {
        problem = "the \"" + hook + "\" handler must return a number or nothing (it returned a " + luaL_typename(L, -1) + ")";
        break;
      }
      lua_pop(L, 1);
    }
    if (!problem.empty()) {
      d.Fail(m, problem);
    } else {
      lua_settop(L, top);
    }
  }
  return value;
}

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
