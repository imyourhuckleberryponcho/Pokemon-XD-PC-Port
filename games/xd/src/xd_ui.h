#ifndef XD_UI_H
#define XD_UI_H

#include <imgui.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace xd::ui {

inline constexpr uint8_t kText[4] = {0xF0, 0xF0, 0xF0, 0xFF};
inline constexpr uint8_t kSoft[4] = {0xC0, 0xC0, 0xC8, 0xFF};
inline constexpr uint8_t kDim[4] = {0x88, 0x88, 0x90, 0xFF};
inline constexpr uint8_t kGold[4] = {0xFF, 0xD8, 0x68, 0xFF};
inline constexpr uint8_t kGood[4] = {0x98, 0xEC, 0x98, 0xFF};
inline constexpr uint8_t kBad[4] = {0xFF, 0x98, 0x88, 0xFF};

ImVec2 At(float x, float y);
float Scale();

void Backdrop(ImDrawList* dl);
void Dim(ImDrawList* dl, uint8_t alpha);
void Header(ImDrawList* dl, const std::string& title, const std::string& right = "");
void Window(ImDrawList* dl, float x0, float y0, float x1, float y1, bool accent = false);
void TitledWindow(ImDrawList* dl, float x0, float y0, float x1, float y1, const std::string& title, bool accent = false);
void RowSel(ImDrawList* dl, float x0, float x1, float y, float h, bool cursor = true);
void Cursor(ImDrawList* dl, float cx, float cy);
void Button(ImDrawList* dl, float x, float y, float w, const std::string& label, bool selected, bool enabled = true);
void Value(ImDrawList* dl, float x, float y, float w, const std::string& value, bool selected);
void HintBar(ImDrawList* dl, const std::string& hints);
void Ring(ImDrawList* dl, float cx, float cy, float r);
uint8_t Pulse();
void Text(ImDrawList* dl, float x, float y, const std::string& t, const uint8_t* col, float scale = 1.f, float maxW = 0.f, int font = 0);
void TextC(ImDrawList* dl, float x, float y, float w, const std::string& t, const uint8_t* col, float scale = 1.f, int font = 0);
int Para(ImDrawList* dl, float x, float y, float w, const std::string& t, const uint8_t* col, float scale, float pitch, int maxLines);
float Width(const std::string& t, float scale = 1.f, int font = 0);
float Prompt(ImDrawList* dl, float x, float cy, std::initializer_list<const char*> keys, const std::string& word);

int NpcPages(const std::string& title, const std::string& text);
bool NpcMessage(ImDrawList* dl, const std::string& title, const std::string& text, int page);
int ChoiceRowsFit();
void ChoiceBox(ImDrawList* dl, const std::vector<std::string>& rows, int cursor, int top);

}

#endif
