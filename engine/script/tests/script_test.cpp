#include <script_host.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace engine::script;

namespace {

bool sOk = true;

void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

struct Fixture {
  std::vector<std::string> log;
  Host host{{"test.value", "test.other"}, Limits{}, [this](const std::string& line) { log.push_back(line); }};
  bool Add(const std::string& id, const std::string& code, std::string* why = nullptr) {
    std::string error;
    const bool ok = host.AddMod(id, {{"scripts/main.lua", code}}, error);
    if (why != nullptr) *why = error;
    return ok;
  }
  int64_t Call(int64_t value, int64_t n = 7) {
    Value ev = Value::Table();
    ev.Set("who", Value::Table().Set("n", n));
    return host.CallInt("test.value", ev, "value", value);
  }
};

bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}

int main() {
  {
    Fixture f;
    check(f.Add("double", "on('test.value', function(e) return e.value * 2 + e.who.n end)"), "a script loads");
    check(f.host.HasHandlers("test.value") && !f.host.HasHandlers("test.other"), "HasHandlers knows the hooks it has");
    check(f.Call(10) == 27, "the handler's number is the new value (10*2+7)");
    check(f.Call(10) == 27, "and again (no state leaks between calls)");
  }
  {
    Fixture f;
    check(f.Call(123) == 123, "no mod: value unchanged");
    f.Add("quiet", "local x = 1");
    check(f.Call(123) == 123, "a mod without a handler: value unchanged");
    f.Add("nothing", "on('test.value', function(e) end)");
    check(f.Call(123) == 123, "a handler that returns nothing: value unchanged");
  }
  {
    Fixture f;
    f.Add("half", "on('test.value', function(e) return e.value / 2.5 end)");
    check(f.Call(11) == 4, "a float result is truncated (11/2.5 = 4.4 -> 4)");
  }
  {
    Fixture f;
    std::string why;
    const bool ok = f.Add("leaks",
                          "local names = {'io','os','package','require','debug','coroutine','dofile','loadfile','load','loadstring',"
                          "'collectgarbage','pcall','xpcall','utf8','module'}\n"
                          "for _, n in ipairs(names) do if _G[n] ~= nil then error('leak: ' .. n) end end\n"
                          "if string.dump or string.find or string.match or string.gmatch or string.gsub or string.pack then error('leak: string') end\n"
                          "if math.random or math.randomseed or math.sin or math.cos or math.exp or math.log then error('leak: math') end\n"
                          "if os or io then error('leak') end\n"
                          "if getmetatable('').__index ~= string then error('string metatable') end",
                          &why);
    check(ok, "io, os, package, require, debug, load*, pcall, patterns, random: all absent" + (ok ? std::string() : " (" + why + ")"));
    check(f.Add("env", "x = io"), "reading a missing global is just nil");
    check(f.host.LiveMods() == 2, "...and does not stop the mod");
    const bool r_callio = f.Add("callio", "io.open('x')", &why);
    check(!r_callio && Contains(why, "nil value"), "calling io.open is an error: " + why);
    const bool r_req = f.Add("req", "require('os')", &why);
    check(!r_req && Contains(why, "nil value"), "calling require is an error: " + why);
    const bool r_ld = f.Add("ld", "load('return 1')()", &why);
    check(!r_ld && Contains(why, "nil value"), "load is not there");
    const bool r_fmt = f.Add("fmt", "return string.format('%p', {})", &why);
    check(!r_fmt && Contains(why, "not available"), "string.format %p is refused: " + why);
    check(f.Add("fmt2", "assert(string.format('%5.2f|%d|%s', 1.5, 3, 'x') == ' 1.50|3|x')"), "string.format still works");
  }
  {
    Fixture f;
    std::string why;
    std::string error;
    const bool r_bc = f.host.AddMod("bc", {{"scripts/b.lua", std::string("\x1bLua\x54\x00", 6)}}, error);
    check(!r_bc && Contains(error, "binary"), "precompiled bytecode is refused: " + error);

  }
  {
    Fixture f;
    std::string why;
    const bool r_spin = f.Add("spin", "while true do end", &why);
    check(!r_spin && Contains(why, "ran too long"), "an endless loop at load is stopped: " + why);
    check(f.host.LiveMods() == 0, "...and that mod is off");
    f.Add("good", "on('test.value', function(e) return e.value + 1 end)");
    f.Add("slow", "on('test.value', function(e) while true do end end)");
    f.Add("later", "on('test.value', function(e) return e.value * 10 end)");
    check(f.Call(5) == 60, "an endless loop in a handler is stopped; other mods still answer (5+1, slow skipped, *10)");
    check(f.host.LiveMods() == 2, "only the looping mod is off");
    const auto failed = f.host.TakeFailures();
    check(failed.size() == 2 && failed[1].modId == "slow" && Contains(failed[1].message, "ran too long"), "the failure is reported with the mod's name");
    check(f.Call(5) == 60, "and the next call works without it");
  }
  {
    Fixture f;
    check(f.Add("pcalls", "on('test.value', function(e) while true do local ok = pcall(function() while true do end end) end end)"), "loads");
    f.Call(1);
    check(f.host.LiveMods() == 0, "a script cannot catch the stop (no pcall): the mod is off");
  }
  {
    Fixture f;
    std::string why;
    const bool synOk = f.Add("bad", "on('test.value' function", &why);
    check(!synOk && Contains(why, "expected"), "a syntax error is reported: " + why);
    f.Add("boom", "on('test.value', function(e) error('my mistake') end)");
    check(f.Call(9) == 9, "a runtime error leaves the game's value");
    auto failed = f.host.TakeFailures();
    check(failed.size() >= 1 && Contains(failed.back().message, "my mistake"), "the error text reaches the failure list: " + failed.back().message);
    check(f.host.LiveMods() == 0 && !f.host.HasHandlers("test.value"), "the mod is off and has no handlers any more");
    f.Add("str", "on('test.value', function(e) return 'high' end)");
    check(f.Call(9) == 9 && f.host.LiveMods() == 0, "a text as the answer switches the mod off");
    f.Add("nan", "on('test.value', function(e) return 0/0 end)");
    check(f.Call(9) == 9 && f.host.LiveMods() == 0, "NaN as the answer switches the mod off");
    f.Add("nilcall", "on('test.value', function(e) return e.attacker.hp end)");
    check(f.Call(9) == 9 && f.host.LiveMods() == 0, "indexing a missing field switches the mod off");
    const bool r_nohook = f.Add("nohook", "on('battle.nothing', function() end)", &why);
    check(!r_nohook && Contains(why, "no hook called"), "an unknown hook name is an error: " + why);
  }
  {
    Fixture f;
    std::string why;
    const bool r_rep = f.Add("rep", "local s = string.rep('x', 1000000000)", &why);
    check(!r_rep && !why.empty(), "a gigabyte string is refused: " + why);
    const bool r_grow = f.Add("grow", "local t = {} for i = 1, 100000000 do t[i] = {i, i, i} end", &why);
    check(!r_grow && !why.empty(), "growing without end is stopped: " + why);
  }
  {
    Fixture a, b;
    const std::string code =
        "local keys = {} local t = {alpha=1, beta=2, gamma=3, delta=4, eps=5} for k in pairs(t) do keys[#keys+1] = k end\n"
        "local s = table.concat(keys, ',') .. tostring({}) .. tostring(print)\n"
        "on('test.value', function(e) return #s end)";
    a.Add("order", code);
    b.Add("order", code);
    check(a.Call(0) == b.Call(0) && a.Call(0) > 0, "pairs order and tostring give the same on two hosts");
    check(a.host.Digest() == b.host.Digest(), "the digest of the same scripts is the same");
    Fixture c;
    c.Add("order", code + " ");
    check(c.host.Digest() != a.host.Digest(), "another text gives another digest");
  }
  {
    Fixture f;
    f.Add("talk", "on('test.value', function(e) for i = 1, 100 do log('line', i, {}) end return nil end)");
    f.Call(1);
    check(f.log.size() == 16 && Contains(f.log[0], "[script talk] line 1 <table>"), "log: 16 lines a call, tables only by kind (" + std::to_string(f.log.size()) + ")");
  }
  {
    Fixture f;
    Value t = Value::Table();
    t.Set("WATER", 11);
    f.host.SetConstants("TYPE", t);
    f.Add("a", "on('test.value', function(e) return TYPE.WATER end)");
    f.Add("b", "on('test.value', function(e) return e.value + 1 end)");
    check(f.Call(0) == 12, "constants are there, and mods answer in order (11, then +1)");
  }
  std::printf(sOk ? "script_test: all passed\n" : "script_test: FAILED\n");
  return sOk ? 0 : 1;
}
