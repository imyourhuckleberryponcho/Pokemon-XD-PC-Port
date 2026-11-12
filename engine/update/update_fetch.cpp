#include "update.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

bool FilePathOf(const std::string& url, fs::path& out) {
  if (url.rfind("file:///", 0) != 0) {
    return false;
  }
  std::string path;
  for (size_t i = 8; i < url.size(); i++) {
    if (url[i] == '%' && i + 2 < url.size()) {
      path += static_cast<char>(std::strtol(url.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      path += url[i];
    }
  }
  out = fs::path(Wide(path));
  return true;
}

struct Sink {
  std::string* text = nullptr;
  std::FILE* file = nullptr;
  uint64_t limit = 0;
  uint64_t done = 0;
  bool Put(const char* data, size_t size, std::string& error) {
    done += size;
    if (done > limit) {
      error = "the download is larger than expected";
      return false;
    }
    if (text != nullptr) {
      text->append(data, size);
      return true;
    }
    if (std::fwrite(data, 1, size, file) != size) {
      error = "the download couldn't be written to disk (is it full?)";
      return false;
    }
    return true;
  }
};

bool FetchFile(const fs::path& path, Sink& sink, const FetchOptions& options, std::string& error) {
  std::FILE* f = _wfopen(path.c_str(), L"rb");
  if (f == nullptr) {
    error = "the file isn't there: " + path.string();
    return false;
  }
  std::error_code ec;
  const uint64_t total = fs::file_size(path, ec);
  std::vector<char> buffer(1 << 16);
  bool ok = true;
  for (size_t n; ok && (n = std::fread(buffer.data(), 1, buffer.size(), f)) > 0;) {
    ok = sink.Put(buffer.data(), n, error);
    if (ok && options.progress && !options.progress(sink.done, total)) {
      error = "stopped";
      ok = false;
    }
  }
  std::fclose(f);
  return ok;
}

std::string WinError(const char* what) {
  const DWORD code = GetLastError();
  std::string reason;
  switch (code) {
  case ERROR_WINHTTP_TIMEOUT: reason = "no answer in time"; break;
  case ERROR_WINHTTP_NAME_NOT_RESOLVED: reason = "the server's name wasn't found (offline?)"; break;
  case ERROR_WINHTTP_CANNOT_CONNECT: reason = "couldn't connect"; break;
  case ERROR_WINHTTP_CONNECTION_ERROR: reason = "the connection broke off"; break;
  case ERROR_WINHTTP_SECURE_FAILURE: reason = "the server's certificate wasn't accepted"; break;
  default: reason = "Windows error " + std::to_string(code); break;
  }
  return std::string(what) + ": " + reason;
}

bool FetchHttp(const std::string& url, Sink& sink, const FetchOptions& options, std::string& error) {
  const std::wstring wurl = Wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256]{}, path[4096]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  wchar_t extra[4096]{};
  parts.lpszExtraInfo = extra;
  parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
    error = "the address isn't usable: " + url;
    return false;
  }
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  HINTERNET session = WinHttpOpen(Wide(options.userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) {
    session = WinHttpOpen(Wide(options.userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (session == nullptr) {
    error = WinError("no connection possible");
    return false;
  }
  WinHttpSetTimeouts(session, options.timeoutMs, options.timeoutMs, options.timeoutMs, options.timeoutMs);
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
    protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
  }
  bool ok = false;
  HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
  HINTERNET request = nullptr;
  if (connect == nullptr) {
    error = WinError("couldn't reach the server");
  } else {
    const std::wstring object = std::wstring(path) + extra;
    request = WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (request == nullptr) {
      error = WinError("couldn't ask the server");
    }
  }
  if (request != nullptr) {
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
      error = WinError("the server didn't answer");
    } else {
      DWORD status = 0, size = sizeof(status);
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &size, WINHTTP_NO_HEADER_INDEX);
      wchar_t length[32]{};
      DWORD lengthSize = sizeof(length);
      uint64_t total = 0;
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &lengthSize,
                              WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(length, nullptr, 10);
      }
      if (status != 200) {
        error = status == 404 ? "the server has no such file (404)" : "the server answered " + std::to_string(status);
      } else if (total > sink.limit) {
        error = "the download is larger than expected";
      } else {
        std::vector<char> buffer(1 << 16);
        ok = true;
        for (;;) {
          DWORD got = 0;
          if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            error = WinError("the download broke off");
            ok = false;
            break;
          }
          if (got == 0) {
            break;
          }
          if (!sink.Put(buffer.data(), got, error)) {
            ok = false;
            break;
          }
          if (options.progress && !options.progress(sink.done, total)) {
            error = "stopped";
            ok = false;
            break;
          }
        }
        if (ok && total != 0 && sink.done != total) {
          error = "the download is incomplete";
          ok = false;
        }
      }
    }
    WinHttpCloseHandle(request);
  }
  if (connect != nullptr) {
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return ok;
}

bool Fetch(const std::string& url, Sink& sink, const FetchOptions& options, std::string& error) {
  fs::path file;
  if (FilePathOf(url, file)) {
    return FetchFile(file, sink, options, error);
  }
  if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
    return FetchHttp(url, sink, options, error);
  }
  error = "the address isn't usable: " + url;
  return false;
}

}

bool FetchToString(const std::string& url, const FetchOptions& options, std::string& out, std::string& error) {
  out.clear();
  Sink sink;
  sink.text = &out;
  sink.limit = options.maxBytes;
  if (!Fetch(url, sink, options, error)) {
    out.clear();
    return false;
  }
  return true;
}

bool FetchToFile(const std::string& url, const fs::path& file, const FetchOptions& options, std::string& error) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path part = fs::path(file).concat(L".download");
  fs::remove(part, ec);
  Sink sink;
  sink.file = _wfopen(part.c_str(), L"wb");
  sink.limit = options.maxBytes;
  if (sink.file == nullptr) {
    error = "the download couldn't be saved in " + file.parent_path().string();
    return false;
  }
  bool ok = Fetch(url, sink, options, error);
  if (std::fclose(sink.file) != 0 && ok) {
    error = "the download couldn't be written to disk (is it full?)";
    ok = false;
  }
  if (ok) {
    fs::remove(file, ec);
    fs::rename(part, file, ec);
    if (ec) {
      error = "the download couldn't be saved: " + ec.message();
      ok = false;
    }
  }
  if (!ok) {
    fs::remove(part, ec);
    fs::remove(file, ec);
  }
  return ok;
}

}
