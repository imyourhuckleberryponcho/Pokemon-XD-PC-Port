#include "xd_test_run.h"

#include <port_audio.h>
#include <port_user_folder.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" void PortXD_LoadDspCoef(void) {
  namespace fs = std::filesystem;
  const char* given = std::getenv("XD_DSP_COEF");
  fs::path file;
  if (given != nullptr && given[0] != '\0') {
    if (std::string(given) == "0") {
      std::fprintf(stderr, "[audio] resampler table: the port's own (XD_DSP_COEF=0)\n");
      return;
    }
    file = fs::path(given);
  } else if (xd::TestRun()) {
    return;
  } else {
    file = PortUser_Folder("Pokemon XD") / "dsp" / "dsp_coef.bin";
  }
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) {
    if (given != nullptr && given[0] != '\0') {
      std::fprintf(stderr, "[audio] resampler table: the port's own (XD_DSP_COEF: no file %s)\n", given);
    } else {
      std::fprintf(stderr, "[audio] resampler table: the port's own (no console dump in the data folder: "
                           "dsp%cdsp_coef.bin)\n", static_cast<char>(fs::path::preferred_separator));
    }
    return;
  }
  std::vector<uint8_t> bytes;
  {
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const int order = PortDsp_UseCoefDump(bytes.data(), bytes.size());
  if (order == 0) {
    std::fprintf(stderr, "[audio] resampler table: the port's own. dsp_coef.bin (%zu bytes) is not a dump of the "
                         "audio chip's coefficient ROM (4096 bytes, as DSPSpy writes it)\n", bytes.size());
    return;
  }
  std::fprintf(stderr, "[audio] resampler table: the console's own, from the dump dsp_coef.bin%s\n",
               order == 2 ? " (low byte first)" : "");
}
