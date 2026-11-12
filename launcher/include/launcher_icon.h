#ifndef LAUNCHER_ICON_H
#define LAUNCHER_ICON_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher::icon {

struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
  bool Empty() const { return width <= 0 || height <= 0; }
};

Image DecodeRGB5A3(const uint8_t* data, int width, int height);
Image DecodeCI8(const uint8_t* data, const uint8_t* palette, int width, int height);

Image DecodeGsTexture(const uint8_t* file, size_t size);

Image Crop(const Image& in, int x, int y, int width, int height);
Image AlphaFromBrightness(const Image& in, int opaqueAt = 48);
Image Trim(const Image& in);

Image FocusTop(const Image& in);

Image Scale2x(const Image& in);
Image Scale3x(const Image& in);
Image UpscaleTo(const Image& in, int target);
Image Shrink(const Image& in, int width, int height);
Image Square(const Image& in);

Image Framed(const Image& picture, int inner = 236);

std::vector<Image> IconSizes(const Image& square, const Image* forSmall = nullptr);

bool WriteIco(const std::filesystem::path& file, const std::vector<Image>& sizes);

}

#endif
