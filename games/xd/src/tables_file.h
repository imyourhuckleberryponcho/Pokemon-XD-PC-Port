#ifndef XD_TABLES_FILE_H
#define XD_TABLES_FILE_H

#include <filesystem>
#include <string>

namespace xd::tablesfile {

bool Write(const std::filesystem::path& file, const std::string& stamp);

}

#endif
