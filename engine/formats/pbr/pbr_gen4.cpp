#include "pbr_gen4.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "pbr_eval.h"

namespace pbr {

namespace {

const std::set<std::string> NOT_STANDIN = {"metamon", "migawari", "eipam", "tamago"};
constexpr int GEN4_FIRST = 387, GEN4_LAST = 493;

double py_sum(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double f = 0.0, c = 0.0;
  for (double x : v) {
    double t = f + x;
    if (std::fabs(f) >= std::fabs(x)) c += (f - t) + x;
    else c += (x - t) + f;
    f = t;
  }
  if (c != 0.0 && std::isfinite(c)) f += c;
  return f;
}

std::map<int, std::string> make_plan_of() {
  std::map<int, std::string> m;
  const std::vector<std::pair<std::string, std::string>> t = {
      {"biped", "390-395 400-402 407-409 419 424 427 428 439-440 443-448 453 454 459-461 463-467 475 484 486"},
      {"quad", "387-389 399 403-405 410 411 417 418 431 432 434 435 449 450 470 471 473 483 485 492 493"},
      {"flyer", "396-398 414-416 430 441 468 469 472 488"},
      {"floater", "425 426 429 433 436 437 442 462 474 476-482 491"},
      {"arthropod", "451 452"},
      {"fish", "456-458 489 490"},
      {"blob", "406 412 413 420-423 455"},
      {"serpent", "487"}};
  for (auto& [plan, list] : t) {
    size_t i = 0;
    while (i < list.size()) {
      size_t j = list.find(' ', i);
      if (j == std::string::npos) j = list.size();
      std::string r = list.substr(i, j - i);
      i = j + 1;
      size_t d = r.find('-');
      int a = atoi(r.c_str()), b = d == std::string::npos ? a : atoi(r.c_str() + d + 1);
      for (int k = a; k <= b; k++) m[k] = plan;
    }
  }
  return m;
}

std::map<std::string, std::vector<int>> make_standins() {
  std::map<std::string, std::vector<int>> m;
  const std::vector<std::pair<std::string, std::string>> t = {
      {"biped", "7-9 25 26 56 57 62 66-68 106 107 124-126 143 150 158-160 214 237 248 254-260 280-282 287-289 296 297 307 308"},
      {"quad", "1-3 58 59 77 78 111 112 128 133-136 152-154 196 197 216 217 231 232 241 243-245 322 323 335"},
      {"flyer", "12 16-18 21 22 41 42 49 142 144-146 163 164 169 193 198 267 269 276 277 284 333 334 227 249 250 373 330"},
      {"floater", "92 93 200 201 355 356 358 337 338 343 344 351 109 110 81 82 374-376 385 386 151 251"},
      {"arthropod", "14 15 46 47 98 99 123 127 167 168 204 205 213 341 342 347 348"},
      {"fish", "118-121 129 170 171 211 223 224 318-321 339 349 350 366-370 72 73 90 91 116 117 226 340"},
      {"blob", "35 39 40 43-45 50 51 60 61 69-71 74 88 89 100 101 102 113 114 174 175 183 184 189 191 192 194 195 273-275 285 286 "
               "298 316 317 331 332"},
      {"serpent", "23 24 95 130 147 148 208 336"}};
  for (auto& [plan, list] : t) {
    size_t i = 0;
    while (i < list.size()) {
      size_t j = list.find(' ', i);
      if (j == std::string::npos) j = list.size();
      std::string r = list.substr(i, j - i);
      i = j + 1;
      if (r.empty()) continue;
      size_t d = r.find('-');
      int a = atoi(r.c_str()), b = d == std::string::npos ? a : atoi(r.c_str() + d + 1);
      for (int k = a; k <= b; k++) m[plan].push_back(k);
    }
  }
  return m;
}

double jac(const std::vector<std::string>& a_, const std::vector<std::string>& b_) {
  std::set<std::string> a(a_.begin(), a_.end()), b(b_.begin(), b_.end());
  std::set<std::string> u = a;
  u.insert(b.begin(), b.end());
  size_t inter = 0;
  for (auto& x : a)
    if (b.count(x)) inter++;
  return u.empty() ? 1.0 : (double)inter / (double)u.size();
}

bool has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool ends_with(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }

}

std::string gen4_tokens(const std::string& name) {
  std::string n = name;
  for (auto& c : n) c = (char)tolower((unsigned char)c);
  for (const char* p : {"left", "right", "l", "r"}) {
    size_t k = strlen(p);
    if (n.compare(0, k, p) == 0) {
      n = n.substr(k);
      if (!n.empty() && n[0] == '_') n = n.substr(1);
      break;
    }
  }
  size_t best = std::string::npos;
  for (const char* p : {"_l", "_r", "_left", "_right"})
    if (ends_with(n, p)) best = std::min(best, n.size() - strlen(p));
  if (best != std::string::npos) n = n.substr(0, best);
  size_t e = n.size();
  while (e > 0 && isdigit((unsigned char)n[e - 1])) e--;
  return n.substr(0, e);
}

Gen4Features gen4_features(const std::vector<uint8_t>& sdr_bytes) {
  Gen4Features f;
  sdr::Sdr sdr(sdr_bytes);
  Eval ev(sdr, "joint8_under_joint");
  std::vector<std::string> acts;
  for (auto& a : sdr.actions)
    if (a.name.rfind("tx_", 0) != 0) acts.push_back(a.name);
  int idle = -1;
  for (size_t i = 0; i < sdr.actions.size(); i++)
    if (sdr.actions[i].name == "wait") { idle = (int)i; break; }
  if (idle < 0)
    for (size_t i = 0; i < sdr.actions.size(); i++)
      if (sdr.actions[i].name == "kuchu_wait") { idle = (int)i; break; }
  if (idle < 0) idle = 0;
  Pose po = ev.sample(idle);
  M4 eye = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::vector<double> w = ev.world(po);
  for (size_t b = 0; b + 16 <= w.size(); b += 16) {
    M4 r = mm4(eye.data(), &w[b]);
    std::copy(r.begin(), r.end(), w.begin() + b);
  }
  std::vector<double> bo = boxes(ev.points(w, po.frames, po.visible.data()), po.frames);
  std::array<double, 3> s = {0, 0, 0};
  for (int fr = 0; fr < po.frames; fr++)
    for (int k = 0; k < 3; k++) s[k] += bo[(size_t)fr * 6 + 3 + k] - bo[(size_t)fr * 6 + k];
  for (int k = 0; k < 3; k++) s[k] = s[k] / (double)po.frames;
  std::set<std::string> ef, bones;
  for (auto& n : sdr.nodes) {
    if (n.name.rfind("ef_", 0) == 0) ef.insert(n.name);
    if (n.kind == 2) bones.insert(gen4_tokens(n.name));
  }
  f.ef.assign(ef.begin(), ef.end());
  f.bones.assign(bones.begin(), bones.end());
  f.actions = acts;
  f.ext = s;
  f.nodes = (int)sdr.nodes.size();
  f.ok = true;
  return f;
}

double gen4_score(const Gen4Features& g, const Gen4Features& c) {
  const auto& ge = g.ext;
  const auto& ce = c.ext;
  double gh = std::max(std::max(ge[0], ge[1]), ge[2]), ch = std::max(std::max(ce[0], ce[1]), ce[2]);
  double size = std::fabs(std::log(std::max(gh, 1e-3) / std::max(ch, 1e-3)));
  double gs = std::max(py_sum({ge[0], ge[1], ge[2]}), 1e-3), cs = std::max(py_sum({ce[0], ce[1], ce[2]}), 1e-3);
  std::vector<double> terms;
  for (int i = 0; i < 3; i++) terms.push_back(std::fabs(std::log(std::max(ge[i], 1e-3) / gs) - std::log(std::max(ce[i], 1e-3) / cs)));
  double asp = py_sum(terms);
  int air = (has(g.actions, "kuchu_wait") != has(c.actions, "kuchu_wait")) ? 1 : 0;
  return (((((3.0 * jac(g.ef, c.ef) + 2.0 * jac(g.bones, c.bones)) + 2.0 * jac(g.actions, c.actions)) - 1.5 * size) - 1.0 * asp) - 1.5 * air);
}

namespace {

double check_percent(const std::vector<std::string>& lines, bool* found) {
  for (auto& l : lines) {
    if (l.rfind("CHECK: converted pose", 0) != 0) continue;
    size_t p = l.find("distance ");
    if (p == std::string::npos) continue;
    *found = true;
    return atof(l.c_str() + p + 9);
  }
  *found = false;
  return 0;
}

}

std::vector<Gen4Result> gen4_run(const std::vector<Gen4Source>& all, const std::set<std::string>& converted, int jobs,
                                 const std::function<void(int, int, const std::string&)>& progress) {
  static const std::map<int, std::string> PLAN_OF = make_plan_of();
  static const std::map<std::string, std::vector<int>> PLAN_STANDINS = make_standins();
  struct Row { int dex; std::string name; bool x; const Gen4Source* src; };
  std::vector<Row> rows;
  for (auto& s : all) {
    if (s.member.empty() || s.archive.rfind("pkx_", 0) != 0 || s.archive.size() < 7) continue;
    if (!(isdigit((unsigned char)s.archive[4]) && isdigit((unsigned char)s.archive[5]) && isdigit((unsigned char)s.archive[6]))) continue;
    std::string nm = s.member.substr(0, s.member.find(':'));
    rows.push_back({atoi(s.archive.substr(4, 3).c_str()), nm, s.has_xd, &s});
  }
  std::map<std::string, const Row*> by_name;
  std::map<int, std::string> by_dex;
  for (auto& r : rows) {
    if (!by_name.count(r.name)) by_name[r.name] = &r;
    if (r.x) by_dex[r.dex] = r.name;
  }
  std::map<std::string, Gen4Features> feats;
  auto feature_of = [&](const std::string& n) -> const Gen4Features& {
    auto it = feats.find(n);
    if (it != feats.end()) return it->second;
    Gen4Features f;
    auto b = by_name.find(n);
    if (b != by_name.end()) {
      try {
        f = gen4_features(*b->second->src->sdr);
      } catch (const std::exception&) {
        f = Gen4Features();
      }
    }
    return feats[n] = f;
  };
  std::vector<std::string> cands;
  for (auto& n : converted)
    if (!NOT_STANDIN.count(n) && feature_of(n).ok) cands.push_back(n);
  struct Plan { int dex; std::string name, standin; };
  std::vector<Plan> plan;
  for (auto& r : rows) {
    if (!(GEN4_FIRST <= r.dex && r.dex <= GEN4_LAST) || r.x) continue;
    const Gen4Features& g = feature_of(r.name);
    if (!g.ok) {
      plan.push_back({r.dex, r.name, ""});
      continue;
    }
    const std::string& pl = PLAN_OF.at(r.dex);
    std::string best;
    double bs = 0;
    bool first = true;
    for (int d : PLAN_STANDINS.at(pl)) {
      auto it = by_dex.find(d);
      if (it == by_dex.end() || std::find(cands.begin(), cands.end(), it->second) == cands.end()) continue;
      double sc = gen4_score(g, feature_of(it->second));
      if (first || sc > bs) { best = it->second; bs = sc; first = false; }
    }
    if (first) throw std::runtime_error("max() arg is an empty sequence");
    plan.push_back({r.dex, r.name, best});
  }
  std::map<std::string, Gen4Info> roles;
  for (auto& p : plan) {
    if (p.standin.empty() || roles.count(p.standin)) continue;
    const Gen4Source& s = *by_name.at(p.standin)->src;
    sdr::Sdr sdr(*s.sdr);
    ReadResult xd = hsd_read_pkx(*s.xd_pkx, false);
    StandinStats st;
    st.stop_roles = true;
    try {
      convert(sdr, xd.pkx, xd.model, 0.1, [](const std::string&) {}, "joint8_under_joint", 1.0, nullptr, nullptr, nullptr, std::nullopt, nullptr, &st);
    } catch (const StopConversion&) {
    }
    Gen4Info g;
    g.pick_names = st.pick_names;
    g.pick_times = st.pick_times;
    g.roles = st.roles;
    roles[p.standin] = g;
  }
  std::vector<const Plan*> todo;
  for (auto& p : plan)
    if (!p.standin.empty()) todo.push_back(&p);
  std::vector<Gen4Result> results(todo.size());
  std::atomic<int> next(0), done(0);
  std::mutex mu;
  int total = (int)todo.size();
  auto work = [&]() {
    while (true) {
      int k = next.fetch_add(1);
      if (k >= total) return;
      const Plan& p = *todo[k];
      Gen4Result r;
      r.name = p.name;
      r.standin = p.standin;
      r.state = "converted";
      auto t0 = std::chrono::steady_clock::now();
      std::vector<std::string> lines;
      try {
        const Gen4Info& g4 = roles.at(p.standin);
        const Gen4Source& me = *by_name.at(p.name)->src;
        const Gen4Source& st = *by_name.at(p.standin)->src;
        lines.push_back("stand-in: " + p.standin);
        ConvertFileResult cf = convert_file(*me.sdr, *st.xd_pkx, p.name, [&](const std::string& s) { lines.push_back(s); }, 0.1,
                                            "joint8_under_joint", FILE_CAP, &g4);
        bool f1;
        double err = check_percent(lines, &f1);
        if (f1 && err > 3.0) {
          std::vector<std::string> lines2;
          Gen4Info g2 = g4;
          g2.comp_t = true;
          ConvertFileResult cf2 = convert_file(*me.sdr, *st.xd_pkx, p.name, [&](const std::string& s) { lines2.push_back(s); }, 0.1,
                                               "joint8_under_joint", FILE_CAP, &g2);
          bool f2;
          double err2 = check_percent(lines2, &f2);
          if (f2 && err2 < err) {
            cf = std::move(cf2);
            char buf[200];
            snprintf(buf, sizeof buf, "INFO: scale compensation on translations too: pose error %.2f %% (was %.2f %%)", err2, err);
            lines = lines2;
            lines.push_back(buf);
          } else {
            char buf[200];
            if (f2) snprintf(buf, sizeof buf, "NOTE: pose error %.2f %%; the other compensation rule gave %s: kept", err, std::to_string(err2).c_str());
            else snprintf(buf, sizeof buf, "NOTE: pose error %.2f %%; the other compensation rule gave None: kept", err);
            lines.push_back(buf);
          }
        }
        r.data = std::move(cf.data);
      } catch (const std::exception& ex) {
        r.state = "refused";
        r.data.clear();
        lines.push_back(std::string("REFUSED: Error: ") + ex.what());
      }
      r.lines = std::move(lines);
      r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      std::lock_guard<std::mutex> lk(mu);
      results[k] = std::move(r);
      progress(++done, total, p.name);
    }
  };
  jobs = std::max(1, std::min(jobs, total));
  if (jobs <= 1) {
    work();
  } else {
    std::vector<std::thread> th;
    for (int i = 0; i < jobs; i++) th.emplace_back(work);
    for (auto& t : th) t.join();
  }
  return results;
}

}
