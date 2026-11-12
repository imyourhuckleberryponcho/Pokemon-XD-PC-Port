#include "port_zip.h"

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>

#include <string>

namespace {

template <class T> void Release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

}

namespace port_zip {

bool AsJpeg(const std::string& picture, int quality, std::string& jpeg) {
  jpeg.clear();
  if (picture.empty() || picture.size() > 0x7FFFFFFF) {
    return false;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  IWICImagingFactory* factory = nullptr;
  IWICStream* input = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* source = nullptr;
  IWICFormatConverter* bgr = nullptr;
  IStream* output = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* options = nullptr;
  UINT width = 0, height = 0;
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  bool ok =
      SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateStream(&input)) &&
      SUCCEEDED(input->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(picture.data())),
                                            static_cast<DWORD>(picture.size()))) &&
      SUCCEEDED(factory->CreateDecoderFromStream(input, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &source)) && SUCCEEDED(source->GetSize(&width, &height)) &&
      SUCCEEDED(factory->CreateFormatConverter(&bgr)) &&
      SUCCEEDED(bgr->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)) &&
      SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &output)) &&
      SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) &&
      SUCCEEDED(encoder->Initialize(output, WICBitmapEncoderNoCache)) &&
      SUCCEEDED(encoder->CreateNewFrame(&frame, &options));
  if (ok) {
    PROPBAG2 name = {};
    name.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = static_cast<float>(quality < 1 ? 1 : quality > 100 ? 100 : quality) / 100.0f;
    ok = SUCCEEDED(options->Write(1, &name, &value)) && SUCCEEDED(frame->Initialize(options)) &&
         SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
         IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) && SUCCEEDED(frame->WriteSource(bgr, nullptr)) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  }
  if (ok) {
    STATSTG stat = {};
    LARGE_INTEGER zero = {};
    ok = SUCCEEDED(output->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 &&
         SUCCEEDED(output->Seek(zero, STREAM_SEEK_SET, nullptr));
    if (ok) {
      jpeg.resize(static_cast<size_t>(stat.cbSize.QuadPart));
      ULONG read = 0;
      ok = SUCCEEDED(output->Read(jpeg.data(), static_cast<ULONG>(jpeg.size()), &read)) && read == jpeg.size();
    }
  }
  Release(options);
  Release(frame);
  Release(encoder);
  Release(output);
  Release(bgr);
  Release(source);
  Release(decoder);
  Release(input);
  Release(factory);
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
  if (!ok) {
    jpeg.clear();
  }
  return ok;
}

}
