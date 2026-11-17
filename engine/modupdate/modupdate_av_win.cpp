#include "modupdate.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <cstdlib>

namespace engine::modupdate {

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

}

AvResult ScanDownload(const fs::path& file, const std::string& sourceUrl) {
  AvResult r;
  const char* skip = std::getenv("XD_MODUPDATE_AV");
  if (skip != nullptr && std::string(skip) == "0") {
    r.text = "the system virus scan was switched off for this test run";
    return r;
  }
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninit = SUCCEEDED(init);
  IAttachmentExecute* exec = nullptr;
  const HRESULT made = CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&exec));
  if (FAILED(made) || exec == nullptr) {
    r.text = "Windows' virus scan interface isn't available here; only the launcher's own checks were made";
    if (uninit) CoUninitialize();
    return r;
  }
  r.available = true;
  const std::wstring path = file.wstring();
  exec->SetLocalPath(path.c_str());
  exec->SetFileName(file.filename().c_str());
  if (!sourceUrl.empty()) exec->SetSource(Wide(sourceUrl).c_str());
  const HRESULT saved = exec->Save();
  std::error_code ec;
  const bool still = fs::exists(file, ec);
  if (SUCCEEDED(saved) && still) {
    r.text = "Windows' virus scan (Windows Security or your antivirus program) found nothing";
  } else if (HRESULT_CODE(saved) == ERROR_CANCELLED) {
    r.blocked = true;
    r.text = "the scan was cancelled";
  } else if (!still) {
    r.blocked = true;
    r.text = "the antivirus program removed the file";
  } else {
    char code[32];
    std::snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(saved));
    r.blocked = true;
    r.text = std::string("Windows' scan refused the file (") + code + ")";
  }
  exec->Release();
  if (uninit) CoUninitialize();
  return r;
}

}
