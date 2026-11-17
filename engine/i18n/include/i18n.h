#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace engine::i18n {

bool SetLanguage(std::string_view id);

const char* Language();

const char* LanguageName(std::string_view id);

const char* Tr(const char* english);
std::string Tr(const std::string& english);

size_t ParseForTest(std::string_view text);
std::string_view EmbeddedLocaleText(std::string_view code);

}
