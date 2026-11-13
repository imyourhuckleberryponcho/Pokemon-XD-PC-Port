#include "update.h"

namespace engine::update {

bool FetchToString(const std::string&, const FetchOptions&, std::string& out, std::string& error) {
  out.clear();
  error = "downloads aren't there on this system yet";
  return false;
}

bool FetchToFile(const std::string&, const std::filesystem::path&, const FetchOptions&, std::string& error) {
  error = "downloads aren't there on this system yet";
  return false;
}

}
