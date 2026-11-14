#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "pbr_to_xd.h"

namespace pbr {

constexpr const char* CONVERTER_VERSION = "1.2.0";
constexpr const char* MOD_ID = "battle-revolution-models";
constexpr const char* PBR_ID = "RPBE01";
constexpr const char* XD_ID = "GXXE01";

class Fail : public std::runtime_error {
 public:
  Fail(const std::string& m, int code = 1) : std::runtime_error(m), code(code) {}
  int code;
};

class DiscSource {
 public:
  virtual ~DiscSource() = default;
  virtual std::string game_id() const = 0;
  virtual std::vector<std::pair<std::string, uint32_t>> files() const = 0;
  virtual std::vector<uint8_t> read(const std::string& path) const = 0;
  virtual void activate() const {}
};
using DiscOpener = std::function<std::unique_ptr<DiscSource>(const std::string& path, const char* what)>;

struct FsysEntry {
  int index = 0;
  uint32_t id = 0, offset = 0, size = 0, packed_size = 0, type = 0;
  bool packed = false;
  std::string full, name;
};
std::vector<FsysEntry> fsys_entries(const std::vector<uint8_t>& data);
std::vector<uint8_t> fsys_extract(const std::vector<uint8_t>& data, const FsysEntry& e);
std::vector<uint8_t> lzss_unpack(const std::vector<uint8_t>& data, bool* ok);

using SharedBytes = std::shared_ptr<const std::vector<uint8_t>>;
struct PairRow {
  std::string name;
  SharedBytes sdr, pkx;
  std::string arch;
  int idx = 0;
  std::string pbr_archive;
};
std::vector<PairRow> read_pairs(const DiscSource& pbr, const DiscSource& xd, std::vector<PairRow>* all = nullptr);

struct RunArgs {
  std::string pbr_disc, xd_disc, out;
  int jobs = 0;
  std::string gen4_out;
  std::string gen4_pack;
};
class XdTables;
using XdTablesFactory = std::function<std::unique_ptr<XdTables>(const DiscSource& xd)>;
void run_models(const RunArgs& a, const DiscOpener& open_disc, const std::function<void(const std::string&)>& say,
                const XdTablesFactory& xd_tables = nullptr);

int pbr_models_main(const std::vector<std::string>& args, const DiscOpener& open_disc, const XdTablesFactory& xd_tables = nullptr);

}
