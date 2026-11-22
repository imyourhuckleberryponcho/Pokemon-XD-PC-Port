#include <script_host.h>
#include <script_names.h>

#include <cstdio>
#include <functional>
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
    const bool ok = host.AddMod(id, {{"scripts/main.luau", code}}, error);
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
                          "'collectgarbage','pcall','xpcall','utf8','module','getfenv','setfenv','newproxy','buffer','vector','gcinfo'}\n"
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
    check(!r_callio && Contains(why, "nil"), "calling io.open is an error: " + why);
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
    const bool r_bc = f.host.AddMod("bc", {{"scripts/b.luau", std::string("\x1bLua\x54\x00", 6)}}, error);
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
    check(!synOk && Contains(why, "xpected") && Contains(why, "scripts/main.luau:1"), "a syntax error is reported with the file and line: " + why);
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
  {
    Fixture f;
    f.host.SetApi("get_flag", [](const std::string& mod, const std::vector<Value>& args, std::string& error) -> Value {
      if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "get_flag(number)"; return Value(); }
      return Value::Int(args[0].integer * 10 + (mod == "quest" ? 1 : 2));
    });
    f.host.SetApi("party", [](const std::string&, const std::vector<Value>&, std::string&) -> Value {
      Value list = Value::List();
      list.Add(Value::Table().Set("species", 133).Set("level", 15));
      list.Add(Value::Table().Set("species", 25).Set("level", 9));
      return list;
    });
    check(f.Add("quest",
                "story_function('has_pikachu', function(extra)\n"
                "  local n = 0\n"
                "  for _, p in ipairs(party()) do if p.species == 25 then n = n + p.level end end\n"
                "  return n + extra + get_flag(4)\n"
                "end)\n"
                "story_function('truth', function() return true end)\n"
                "story_function('nothing', function() end)"),
          "a mod registers story functions");
    check(f.Add("other", "story_function('has_pikachu', function() return 1000 end) story_function('only_other', function() return 5 end)"),
          "a second mod may use the same name");
    int64_t r = -1;
    std::string err;
    check(f.host.HasFunction("has_pikachu") && f.host.HasFunction("quest:truth") && !f.host.HasFunction("quest:only_other") &&
              f.host.HasFunction("only_other") && !f.host.HasFunction("nope"),
          "HasFunction: plain name or mod:name");
    check(f.host.CallFunction("has_pikachu", {Value::Int(100)}, r, err) && r == 9 + 100 + 41, "the first mod's function runs with the API's answers (150)");
    check(f.host.CallFunction("other:has_pikachu", {}, r, err) && r == 1000, "mod:name picks the mod");
    check(f.host.CallFunction("quest:truth", {}, r, err) && r == 1, "true is 1");
    check(f.host.CallFunction("quest:nothing", {}, r, err) && r == 0, "nothing is 0");
    check(!f.host.CallFunction("quest:missing", {}, r, err) && Contains(err, "no story function") && r == 0, "a missing function: false + a message");
    check(f.host.LiveMods() == 2, "no mod was switched off so far");
  }
  {
    Fixture f;
    f.Add("bad", "story_function('boom', function() local x = nil; return x.y end)");
    f.Add("good", "story_function('boom', function() return 7 end)");
    int64_t r = -1;
    std::string err;
    check(!f.host.CallFunction("bad:boom", {}, r, err) && Contains(err, "boom") && f.host.LiveMods() == 1, "an error in a story function: false, mod off");
    check(f.host.CallFunction("boom", {}, r, err) && r == 7, "the other mod's function still runs");
    Fixture g;
    g.Add("loop", "story_function('spin', function() while true do end end)");
    check(!g.host.CallFunction("spin", {}, r, err) && Contains(err, "ran too long"), "a story function has an instruction budget");
    Fixture h;
    std::string why;
    check(!h.Add("twice", "story_function('a', function() end) story_function('a', function() end)", &why) && Contains(why, "twice"),
          "a name registered twice is refused");
    Fixture k;
    k.Add("api", "story_function('e', function() return get_flag() end)");
    k.host.SetApi("get_flag", [](const std::string&, const std::vector<Value>&, std::string& error) -> Value { error = "no"; return Value(); });
    check(k.host.LiveMods() == 1, "(an API added after AddMod is not seen by that mod: no crash)");
  }
  {
    check(NameIdentifier("Mr. Mime") == "MR_MIME" && NameIdentifier("ELECTR") == "ELECTR", "NameIdentifier: capitals, dots and spaces to _");
    check(NameIdentifier("Nidoran\xE2\x99\x80") == "NIDORAN_F" && NameIdentifier("Nidoran\xE2\x99\x82") == "NIDORAN_M", "NameIdentifier: female / male signs");
    check(NameIdentifier("Pok\xC3\xA9 Ball") == "POKE_BALL" && NameIdentifier("1st") == "_1ST" && NameIdentifier("?").empty() && NameIdentifier("---").empty(),
          "NameIdentifier: accents, leading digit, nothing usable");
    check(NameKey("Mr. Mime") == "mrmime" && NameKey("MR_MIME") == "mrmime", "NameKey ignores case, spaces and signs");
    Names names;
    names.Add("type", {{"NORMAL", 0}, {"FIRE", 10}, {"?", 9}, {"GRASS", 12}});
    names.Add("species", {{"EEVEE", 133}, {"MR. MIME", 122}, {"Eevee", 900}, {"Zorblat", 415}});
    names.Add("move", {{"TACKLE", 33}});
    check(names.Count("type") == 3 && names.Count("species") == 4, "a text with no usable name is left out; a repeated one is kept as _2");
    check(names.NameOf("species", 900) == "EEVEE_2" && names.NameOf("species", 133) == "EEVEE" && names.NameOf("type", 9).empty(), "NameOf; the first keeps the plain name");
    int64_t v = 0;
    check(names.Find("species", "mr mime", v) && v == 122 && names.Find("species", "Eevee", v) && v == 133 && !names.Find("species", "nope", v), "Find: letter case, spaces and signs don't count; first wins");
    check(!names.Notes().empty(), "Notes say what was skipped or renamed");
    Fixture f;
    names.Install(f.host);
    check(f.Add("n", R"(
      local seen = {}
      on('test.value', function(e)
        local sum = TYPE.GRASS * 1000 + SPECIES.EEVEE + MOVE.TACKLE * 100000
        if name_of('species', 133) ~= 'EEVEE' or name_of('species', 7) ~= nil then return -1 end
        if find('species', 'Zorblat') ~= 415 or find('species', 'nothing') ~= nil then return -2 end
        local n = 0
        for k, v in pairs(TYPE) do n = n + 1 end
        if n ~= 3 then return -3 end
        return sum
      end))"), "a script using the names loads");
    check(f.Call(0) == 3312133, "TYPE.GRASS, SPECIES.EEVEE, MOVE.TACKLE, name_of, find and pairs work");
    Fixture g;
    names.Install(g.host);
    std::string why;
    g.Add("w", "on('test.value', function(e) TYPE.GRASS = 5 return 1 end)", &why);
    check(g.Call(100) == 100 && g.host.LiveMods() == 0, "writing to TYPE is an error: the mod is off, the game's value stays");
    Fixture h;
    names.Install(h.host);
    h.Add("typo", "on('test.value', function(e) return TYPE.GRASSS end)");
    check(h.Call(100) == 100 && h.host.LiveMods() == 0 && Contains(h.host.TakeFailures().at(0).message, "TYPE has no name"), "a misspelt name is a clear error");
    Fixture i;
    names.Install(i.host);
    i.Add("raw", "on('test.value', function(e) if getmetatable(TYPE) ~= 'read-only' then return -2 end rawset(TYPE, 'FIRE', 99) return -1 end)");
    check(i.Call(100) == 100 && i.host.LiveMods() == 0 && Contains(i.host.TakeFailures().at(0).message, "readonly"), "the metatable is hidden and rawset cannot get round it");
    Fixture j;
    names.Install(j.host);
    j.Add("cat", "on('test.value', function(e) return find('flags', 'x') end)");
    check(j.Call(100) == 100 && Contains(j.host.TakeFailures().at(0).message, "no names for"), "an unknown category is an error that lists the ones there are");
    Names other;
    other.Add("type", {{"WATER", 3}});
    Fixture k;
    other.Install(k.host);
    k.Add("o", "on('test.value', function(e) return TYPE.WATER end)");
    check(k.Call(0) == 3, "another game's table through the same API");
    std::vector<NameEntry> list;
    std::vector<std::string> problems;
    const bool ok = ParseNameFile("# comment\n964 STORY_PROGRESS the story value\n0x3d6 SECOND\r\nbad name\n965 lower\n964 STORY_PROGRESS\n", list, problems);
    check(!ok && list.size() == 2 && list[0].value == 964 && list[1].value == 982 && problems.size() == 3, "ParseNameFile: good lines kept, 3 bad ones reported");
  }
  {
    Fixture f;
    f.host.SetPrelude("local on = on\nfunction on_value(fn) on('test.value', function(e) return fn(e.value, e.who.n) end) end\n");
    check(f.Add("short", "on_value(function(v, n) return v + n end)"), "a mod uses the game's helper");
    check(f.Call(10, 5) == 15, "the helper's function gets plain arguments (10 + 5)");
    check(f.host.HasHandlers("test.value"), "the helper registered a handler of the hook");
    Fixture g;
    g.host.SetPrelude("local on = on\nfunction on_other(fn) on('no.such.hook', fn) end\n");
    std::string why;
    check(g.Add("ok", "local x = 1", &why), "a helper that is never called costs the mod nothing");
    check(!g.Add("bad", "on_other(function() end)", &why) && Contains(why, "no hook called"), "a helper that names a hook the game lacks: the mod is switched off with the reason");
    Fixture h;
    h.host.SetPrelude("this is not lua");
    check(!h.Add("x", "local x = 1", &why) && Contains(why, "helpers"), "a prelude that does not load is reported, not run");
    Fixture k;
    k.host.SetPrelude("function helper_only() return 1 end");
    check(k.Add("a", "if io or os or pcall then error('leak') end"), "the prelude runs in the same sandbox");
    check(k.Add("b", "if rawget(_G, 'helper_only') == nil then error('no helper') end"), "and its functions are in the mod's world");
  }
  {
    Fixture f;
    std::string why;
    check(f.Add("typed",
                "--!strict\n"
                "type Hit = {value: number, who: {n: number}}\n"
                "local function bump(h: Hit): number\n"
                "  local total = 0\n"
                "  for i = 1, 5 do\n"
                "    if i == 3 then continue end\n"
                "    total += i\n"
                "  end\n"
                "  local label = if total > 5 then `big {total}` else 'small'\n"
                "  assert(label == 'big 12')\n"
                "  return h.value + total\n"
                "end\n"
                "on('test.value', function(e: Hit): number return bump(e) end)",
                &why),
          "typed Luau loads (type notes, continue, +=, if-expression, string interpolation): " + why);
    check(f.Call(100) == 112, "...and runs (100 + 12)");
    Fixture g;
    check(!g.Add("int", "local x = 7\nlocal y = x & 2", &why) && Contains(why, "scripts/main.luau:2"), "Lua 5.4's & is a syntax error that names the file and line: " + why);
    Fixture g2;
    check(g2.Add("fd", "on('test.value', function(e) return e.value // 2 end)", &why), "// (floor division) works in Luau: " + why);
    check(g2.Call(7) == 3, "7 // 2 is 3");
    Fixture h;
    check(!h.Add("goto", "local x = 1\ngoto done\n::done::", &why), "goto is not understood: " + why);
    Fixture k;
    check(k.Add("old",
                "local t = {}\nfor i = 1, 10 do t[#t + 1] = i * i end\n"
                "local s = 0 for _, v in ipairs(t) do s = s + v end\n"
                "local words = {} for w in pairs({a = 1}) do words[#words + 1] = w end\n"
                "on('test.value', function(e) return s + #words + math.floor(e.value / 2) + math.max(1, 2) + #string.rep('ab', 3) end)", &why),
          "older Lua-style code loads: " + why);
    check(k.Call(10) == 385 + 1 + 5 + 2 + 6, "...and gives what it gave under Lua (385 + 1 + 5 + 2 + 6)");
    Fixture m;
    check(m.Add("bit", "on('test.value', function(e) return bit32.band(e.value, 6) + bit32.lshift(1, 3) end)", &why), "bit32 is there (Luau has no & | << >>): " + why);
    check(m.Call(7) == 6 + 8, "bit32.band / lshift work");
  }
  {
    Fixture f;
    f.Add("for", "on('test.value', function(e) for i = 1, 1000000000 do end return 1 end)");
    f.Add("rec", "local function r(n) return r(n + 1) + 1 end on('test.value', function(e) return r(1) end)");
    check(f.Call(3) == 3 && f.host.LiveMods() == 0, "a very long for loop and a runaway recursion are both stopped (no freeze)");
    const auto failed = f.host.TakeFailures();
    check(failed.size() == 2 && Contains(failed[0].message, "ran too long") && Contains(failed[0].message, "for/scripts/main.luau:1"),
          "the budget message names the mod, file and line: " + (failed.empty() ? std::string() : failed[0].message));
    check(failed.size() == 2 && (Contains(failed[1].message, "stack overflow") || Contains(failed[1].message, "ran too long")), "the recursion ends with a clear message: " + (failed.size() > 1 ? failed[1].message : std::string()));
    Fixture g;
    g.Add("work", "on('test.value', function(e) local s = 0 for i = 1, 50000 do s = s + 1 end return s end)");
    check(g.Call(0) == 50000 && g.host.LiveMods() == 1, "an ordinary amount of work is within the budget");
  }
  {
    Fixture f;
    f.Add("counter", "n = 0 on('test.value', function(e) n = n + 1 return n end)");
    f.Add("other", "on('test.value', function(e) if n ~= nil then error('shared global') end end)");
    check(f.Call(0) == 1 && f.Call(0) == 2 && f.host.LiveMods() == 2, "a script's globals persist between calls and are not seen by another mod");
    Fixture g;
    check(g.Add("lock", "local ok = pcall == nil; on('test.value', function(e) string.upper = nil end)"), "loads");
    g.Call(0);
    check(g.host.LiveMods() == 0, "the library tables are read-only: changing string.upper switches the mod off");
  }
  {
    std::vector<std::string> log;
    Limits lim;
    lim.callInstructions = 1000;
    lim.frameInstructions = 3000;
    Host host{{"test.value"}, lim, [&](const std::string& line) { log.push_back(line); }};
    uint64_t frame = 1;
    host.SetFrameClock([&] { return frame; });
    std::string error;
    check(host.AddMod("busy", {{"scripts/main.luau", "on('test.value', function(e) local s = 0 for i = 1, 200 do s = s + i end return e.value + 1 end)"}}, error), "a busy script loads");
    check(host.AddMod("second", {{"scripts/main.luau", "on('test.value', function(e) return e.value * 10 end)"}}, error), "a second mod loads");
    const auto call = [&](int64_t v) { return host.CallInt("test.value", Value::Table(), "value", v); };
    check(call(1) == 20, "within the frame's budget both mods answer");
    int answered = 0, skippedFirst = -1;
    for (int i = 0; i < 40; i++) {
      const int64_t r = call(1);
      if (r == 20) answered++;
      else if (skippedFirst < 0) skippedFirst = i;
    }
    check(answered > 0 && answered < 40 && skippedFirst > 0, "after the frame's steps are used up the handlers wait: " + std::to_string(answered) + " of 40 calls answered in one frame");
    check(host.LiveMods() == 2, "running out of the frame's steps does not switch a mod off");
    const auto stats = host.Budget();
    check(stats.skipped > 0 || stats.cut > 0, "the host counts what the budget held back");
    bool said = false;
    for (const auto& l : log) said = said || Contains(l, "steps for this game frame");
    check(said, "a log line says the frame's steps are used up");
    frame = 2;
    check(call(1) == 20, "the next frame starts with a full budget");
    std::vector<int> a, b;
    for (int round = 0; round < 2; round++) {
      Host h2{{"test.value"}, lim, {}};
      uint64_t fr = 7;
      h2.SetFrameClock([&] { return fr; });
      std::string err2;
      h2.AddMod("busy", {{"scripts/main.luau", "on('test.value', function(e) local s = 0 for i = 1, 200 do s = s + i end return e.value + 1 end)"}}, err2);
      h2.AddMod("second", {{"scripts/main.luau", "on('test.value', function(e) return e.value * 10 end)"}}, err2);
      std::vector<int>& out = round == 0 ? a : b;
      for (int i = 0; i < 40; i++) out.push_back(static_cast<int>(h2.CallInt("test.value", Value::Table(), "value", 1)));
    }
    check(a == b, "two runs of the same calls are cut at the same place");
    Host h3{{"test.value"}, lim, {}};
    h3.SetFrameClock([&] { return uint64_t(1); });
    std::string err3;
    h3.AddMod("loop", {{"scripts/main.luau", "on('test.value', function(e) while true do end end)"}}, err3);
    h3.CallInt("test.value", Value::Table(), "value", 1);
    check(h3.LiveMods() == 0, "an endless loop is still switched off by the call's own limit");
  }
  {
    Fixture f;
    std::vector<std::string> seen;
    f.host.SetApi("take", [&seen](const std::string&, const std::vector<Value>& args, std::string&) -> Value {
      std::function<std::string(const Value&)> text = [&](const Value& v) -> std::string {
        switch (v.kind) {
        case Value::Kind::Nil: return "nil";
        case Value::Kind::Bool: return v.boolean ? "true" : "false";
        case Value::Kind::Int: return std::to_string(v.integer);
        case Value::Kind::String: return "'" + v.text + "'";
        case Value::Kind::List: {
          std::string out = "[";
          for (const Value& e : v.fieldValues) out += text(e) + ",";
          return out + "]";
        }
        case Value::Kind::Table: {
          std::string out = "{";
          for (size_t i = 0; i < v.fieldNames.size(); i++) out += v.fieldNames[i] + "=" + text(v.fieldValues[i]) + ",";
          return out + "}";
        }
        }
        return "?";
      };
      for (const Value& a : args) seen.push_back(text(a));
      return Value::Bool(true);
    });
    std::string why;
    check(f.Add("tables",
                "take({b = 2, a = {1, 2, 3}, c = {x = 'hi', y = true}}, {{species = 133}, {species = 25, moves = {1, 2}}})\n"
                "on('test.value', function(e) return 1 end)", &why),
          "a script hands tables to a game function: " + why);
    check(seen.size() == 2 && seen[0] == "{a=[1,2,3,],b=2,c={x='hi',y=true,},}", "named fields arrive in name order, lists in order: " + (seen.empty() ? std::string("none") : seen[0]));
    check(seen.size() == 2 && seen[1] == "[{species=133,},{moves=[1,2,],species=25,},]", "a list of tables arrives as a list of tables: " + (seen.size() < 2 ? std::string("none") : seen[1]));
    Fixture g;
    g.host.SetApi("take", [](const std::string&, const std::vector<Value>&, std::string&) -> Value { return Value(); });
    check(!g.Add("mix", "take({1, 2, a = 3})"), "a table with both list and named keys is an error");
    Fixture h;
    h.host.SetApi("take", [](const std::string&, const std::vector<Value>&, std::string&) -> Value { return Value(); });
    check(!h.Add("deep", "take({{{{{1}}}}})"), "a table more than 4 levels deep is an error");
    Fixture k;
    k.host.SetApi("take", [](const std::string&, const std::vector<Value>&, std::string&) -> Value { return Value(); });
    check(!k.Add("big", "local t = {} for i = 1, 500 do t[#t + 1] = i end take(t)"), "a table with more than 400 entries is an error");
    Fixture m;
    m.host.SetApi("take", [](const std::string&, const std::vector<Value>&, std::string&) -> Value { return Value(); });
    check(!m.Add("fn", "take({f = function() end})"), "a function inside a table is an error");
  }
  std::printf(sOk ? "script_test: all passed\n" : "script_test: FAILED\n");
  return sOk ? 0 : 1;
}
