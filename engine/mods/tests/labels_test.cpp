#include <labels.h>

#include <cstdio>
#include <string>

using namespace engine::mods;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}
LabelRow Row(const std::string& label, uint32_t n, const std::string& legacy = "") {
  LabelRow r;
  r.label = label;
  r.number = n;
  r.legacy = legacy;
  r.describe = "row " + (label.empty() ? legacy : label);
  return r;
}
}

int main() {
  Label l;
  std::string why;
  check(ParseLabel("species:base/turtwig", l, why) && l.kind == "species" && l.ns == "base" && l.slug == "turtwig" && l.Text() == "species:base/turtwig", "parse an official label");
  check(ParseLabel("move:ada.zorblat/zap-test_2", l, why) && l.ns == "ada.zorblat" && l.slug == "zap-test_2", "parse a fan label (author.pack, - and _ in the name)");
  check(ParseLabel("item:ada/zorb-berry", l, why), "a namespace of one part is allowed");
  for (const char* bad : {"turtwig", "species:turtwig", "Species:base/turtwig", "species:Base/turtwig", "species:base/Turtwig", "species:base/", "species:/x",
                          "species:a.b.c/x", "species:a b/x", "species:base/tur twig", "species:base/-x", "species:.x/y", "species:x./y", "x:y/z/w"}) {
    check(!ParseLabel(bad, l, why) && !why.empty(), std::string("refused: ") + bad);
  }
  check(!ParseLabel("species:base/" + std::string(100, 'a'), l, why), "refused: too long");
  check(LooksLikeLabel("species:base/x") && !LooksLikeLabel("Mr. Mime") && !LooksLikeLabel("mod-id:Name") && !LooksLikeLabel("#12"), "what counts as label-shaped");

  check(Slugify("Turtwig") == "turtwig" && Slugify("Mr. Mime") == "mr-mime" && Slugify("Farfetch'd") == "farfetchd" &&
            Slugify("Wake-Up Slap") == "wake-up-slap" && Slugify("Porygon2") == "porygon2" && Slugify("  Zap   Test  ") == "zap-test",
        "slugs: lower case, dots and apostrophes dropped, other runs a dash");
  check(Slugify("Nidoran\xE2\x99\x80") == "nidoran-f" && Slugify("Nidoran\xE2\x99\x82") == "nidoran-m" && Slugify("Pok\xC3\xA9 Ball") == "poke-ball" &&
            Slugify("\xE2\x80\xA6") == "" && Slugify("?????") == "" && Slugify("---") == "",
        "slugs: gender signs, accents; nothing usable = empty");
  check(ValidSlug(Slugify("Mime Jr.")) && Slugify("Mime Jr.") == "mime-jr", "a slug is always a valid name part");

  const BaseLabels base("species", {{"Turtwig", 1}, {"Mr. Mime", 2}, {"Twin", 3}, {"Twin", 4}, {"?????", 5}, {"Mr Mime", 6}});
  uint32_t n = 0;
  std::string slug;
  check(base.Find("turtwig", n) && n == 1 && base.SlugOf(1, slug) && slug == "turtwig", "official rows: slug <-> number");
  check(!base.Find("twin", n) && base.Find("twin-3", n) && n == 3 && base.Find("twin-4", n) && n == 4, "a name two rows share: <slug>-<number> each");
  check(!base.SlugOf(5, slug), "a row with no usable name has no label");
  check(base.Find("mr-mime-2", n) && n == 2 && base.Find("mr-mime-6", n) && n == 6, "two names with one slug: both get the number");

  const std::string z = "species:ada.zorb/zorblat", w = "move:ada.zorb/zap", q = "species:other.pack/quux";
  {
    const RemapPlan p = PlanRemap({Row(z, 415), Row(w, 375)}, {Row(z, 415), Row(w, 375)});
    check(!p.Refused() && !p.Needed() && p.steps.empty(), "same labels, same numbers: nothing to do");
  }
  {
    const RemapPlan p = PlanRemap({Row(z, 415), Row(w, 375)}, {Row(q, 415), Row(z, 416), Row(w, 375)});
    check(p.Needed() && p.steps.size() == 1 && p.steps[0].kind == "species" && p.steps[0].from == 415 && p.steps[0].to == 416, "another mod before it: Zorblat 415 -> 416, the move stays");
    check(p.Map("species").size() == 1 && p.Map("species").at(415) == 416 && p.Map("move").empty() && p.Map("item").empty(), "the map is per kind");
  }
  {
    const RemapPlan p = PlanRemap({Row(z, 415), Row(q, 416)}, {Row(q, 415), Row(z, 416)});
    const auto m = p.Map("species");
    check(p.Needed() && m.size() == 2 && m.at(415) == 416 && m.at(416) == 415, "two rows swapped places: both steps (applied at once)");
  }
  {
    const RemapPlan p = PlanRemap({Row(z, 415), Row(q, 416)}, {Row(z, 415)});
    check(p.Refused() && p.problems.size() == 1 && p.problems[0].find(q) != std::string::npos && !p.Needed(), "a label that is not there now: refused, naming it");
  }
  {
    const RemapPlan p = PlanRemap({Row("species:ada.zorb/zorblat", 415)}, {Row("move:ada.zorb/zorblat", 415)});
    check(p.Refused(), "the same name in another kind is another row: refused");
  }
  {
    const RemapPlan p = PlanRemap({Row(z, 415), Row(q, 415)}, {Row(z, 415), Row(q, 416)});
    check(p.Refused(), "a save that gives two labels one number is refused (never guess)");
  }
  {
    const RemapPlan p = PlanRemap({Row("", 415, "zorb|pokemon_data|Zorblat")}, {Row(z, 416, "zorb|pokemon_data|Zorblat")});
    check(p.Needed() && p.steps[0].label == z && p.steps[0].from == 415 && p.steps[0].to == 416, "a save from before labels is matched by mod + table + name, and remapped");
  }
  {
    const RemapPlan p = PlanRemap({Row("", 415, "zorb|pokemon_data|Zorblat")}, {Row(z, 415, "zorb|pokemon_data|Zorblat")});
    check(!p.Refused() && !p.Needed(), "a save from before labels, numbers unchanged: loads as it did");
  }
  {
    const RemapPlan p = PlanRemap({Row("", 415, "zorb|pokemon_data|Zorblat")}, {Row("", 416, "zorb|pokemon_data|Zorblat")});
    check(p.Refused(), "no labels on either side and the number moved: refused (the old rule)");
    const RemapPlan p2 = PlanRemap({Row("", 415, "zorb|pokemon_data|Zorblat")}, {Row(z, 415, "other|pokemon_data|Other")});
    check(p2.Refused(), "a save from before labels whose mod is not on: refused");
  }
  check(PlanRemap({}, {Row(z, 1)}).steps.empty() && !PlanRemap({}, {}).Refused(), "a save with no added rows: nothing to do");

  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
