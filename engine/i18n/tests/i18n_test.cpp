
#include "i18n.h"

#include <cstdio>
#include <cstring>

using namespace engine::i18n;

static int failures = 0;
#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                                                               \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (0)

int main() {
  CHECK(ParseForTest("# c\n= Hello\n> Bonjour\n\n= Two\\nlines\n> Deux\\nlignes\n= Lonely\n") == 2);
  CHECK(std::strcmp(Tr("Hello"), "Bonjour") == 0);
  CHECK(std::strcmp(Tr("Two\nlines"), "Deux\nlignes") == 0);
  CHECK(std::strcmp(Tr("Lonely"), "Lonely") == 0);
  CHECK(std::strcmp(Tr("Unknown"), "Unknown") == 0);
  CHECK(Tr(std::string("Hello")) == "Bonjour");

  CHECK(ParseForTest("= the %s is %d\n> le %d est %s\n= %s: %d\n> %s : %d\n= 100% checklist\n> liste 100%\n= %.1f of %d\n> %d de %.1f\n= %d\n> %lld\n") == 2);
  CHECK(std::strcmp(Tr("the %s is %d"), "the %s is %d") == 0);
  CHECK(std::strcmp(Tr("%s: %d"), "%s : %d") == 0);
  CHECK(std::strcmp(Tr("100% checklist"), "liste 100%") == 0);
  SetLanguage("us");
  CHECK(std::strcmp(Tr("Hello"), "Hello") == 0);
  CHECK(std::strcmp(Language(), "en") == 0);
  for (const char* id : {"ge", "fr", "it", "sp", "jp"}) {
    CHECK(SetLanguage(id));
  }
  CHECK(std::strcmp(Language(), "ja") == 0);
  SetLanguage("us");
  std::printf(failures == 0 ? "i18n_test: OK\n" : "i18n_test: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
