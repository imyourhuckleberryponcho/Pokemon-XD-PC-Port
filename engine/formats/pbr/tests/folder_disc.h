#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>

#include "pbr_batch.h"

class FolderDisc : public pbr::DiscSource {
 public:
  FolderDisc(std::string folder, std::string id) : dir_(std::move(folder)), id_(std::move(id)) {
    for (auto& e : std::filesystem::recursive_directory_iterator(dir_)) {
      if (!e.is_regular_file()) continue;
      std::string rel = std::filesystem::relative(e.path(), dir_).generic_string();
      files_.emplace_back(rel, (uint32_t)e.file_size());
    }
  }
  std::string game_id() const override { return id_; }
  std::vector<std::pair<std::string, uint32_t>> files() const override { return files_; }
  std::vector<uint8_t> read(const std::string& path) const override {
    std::ifstream f(std::filesystem::path(dir_) / path, std::ios::binary);
    if (!f) throw std::runtime_error("read " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }

 private:
  std::string dir_, id_;
  std::vector<std::pair<std::string, uint32_t>> files_;
};
