#include "port_disc.h"

#include <nod.h>

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {

constexpr uint8_t kGameCubeMagic[4] = {0xC2, 0x33, 0x9F, 0x3D};
constexpr uint8_t kWiiMagic[4] = {0x5D, 0x1C, 0x9E, 0xA3};

const char* format_name(NodFormat format) {
  switch (format) {
  case NOD_FORMAT_ISO: return "ISO";
  case NOD_FORMAT_CISO: return "CISO";
  case NOD_FORMAT_GCZ: return "GCZ";
  case NOD_FORMAT_NFS: return "NFS";
  case NOD_FORMAT_RVZ: return "RVZ";
  case NOD_FORMAT_WBFS: return "WBFS";
  case NOD_FORMAT_WIA: return "WIA";
  case NOD_FORMAT_TGC: return "TGC";
  }
  return "unknown";
}

void copy_text(char* dst, size_t dstSize, const char* src, size_t srcSize) {
  size_t n = 0;
  while (n < srcSize && n + 1 < dstSize && src[n] != '\0') {
    dst[n] = src[n];
    n++;
  }
  while (n > 0 && dst[n - 1] == ' ') {
    n--;
  }
  dst[n] = '\0';
}

class Sha1Stream {
public:
  Sha1Stream() {
#ifdef _WIN32
    if (BCryptOpenAlgorithmProvider(&mAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0 ||
        BCryptCreateHash(mAlg, &mHash, nullptr, 0, nullptr, 0, 0) != 0) {
      mHash = nullptr;
    }
#endif
    PortSha1_Begin(&mPortable);
  }
  ~Sha1Stream() {
#ifdef _WIN32
    if (mHash != nullptr) BCryptDestroyHash(mHash);
    if (mAlg != nullptr) BCryptCloseAlgorithmProvider(mAlg, 0);
#endif
  }
  void Add(const uint8_t* data, size_t size) {
#ifdef _WIN32
    if (mHash != nullptr) {
      while (size > 0) {
        const ULONG n = size > 0x40000000 ? 0x40000000 : static_cast<ULONG>(size);
        BCryptHashData(mHash, const_cast<PUCHAR>(data), n, 0);
        data += n, size -= n;
      }
      return;
    }
#endif
    PortSha1_Add(&mPortable, data, size);
  }
  void FinishHex(char out[41]) {
#ifdef _WIN32
    if (mHash != nullptr) {
      UCHAR digest[20];
      BCryptFinishHash(mHash, digest, sizeof(digest), 0);
      for (int i = 0; i < 20; i++) {
        std::snprintf(out + i * 2, 3, "%02x", digest[i]);
      }
      return;
    }
#endif
    PortSha1_FinishHex(&mPortable, out);
  }

private:
#ifdef _WIN32
  BCRYPT_ALG_HANDLE mAlg = nullptr;
  BCRYPT_HASH_HANDLE mHash = nullptr;
#endif
  PortSha1 mPortable;
};

}

extern "C" {

PortDiscKind PortDisc_Probe(const char* path, PortDiscInfo* info, char* error, size_t errorSize) {
  PortDiscInfo out{};
  out.kind = PORT_DISC_UNREADABLE;
  out.format = "unknown";
  if (error != nullptr && errorSize > 0) {
    error[0] = '\0';
  }

  NodHandle* disc = nullptr;
  NodDiscHeader header{};
  NodDiscMeta meta{};
  if (nod_disc_open(path, nullptr, &disc) != NOD_RESULT_OK || nod_disc_header(disc, &header) != NOD_RESULT_OK) {
    if (error != nullptr && errorSize > 0) {
      const char* detail = nod_error_message();
      std::snprintf(error, errorSize, "%s", detail != nullptr ? detail : "unknown reason");
    }
  } else {
    if (std::memcmp(header.gcn_magic, kGameCubeMagic, 4) == 0) {
      out.kind = PORT_DISC_GAMECUBE;
    } else if (std::memcmp(header.wii_magic, kWiiMagic, 4) == 0) {
      out.kind = PORT_DISC_WII;
    } else {
      out.kind = PORT_DISC_NOT_A_DISC;
    }
    copy_text(out.gameId, sizeof(out.gameId), header.game_id, sizeof(header.game_id));
    copy_text(out.title, sizeof(out.title), header.game_title, sizeof(header.game_title));
    out.discNumber = header.disc_num;
    out.version = header.disc_version;
    out.discSize = nod_disc_size(disc);
    if (nod_disc_meta(disc, &meta) == NOD_RESULT_OK) {
      out.format = format_name(meta.format);
      out.complete = meta.lossless;
    }
  }
  nod_free(disc);
  if (info != nullptr) {
    *info = out;
  }
  return out.kind;
}

PortDiscHashResult PortDisc_HashSha1(const char* path, PortDiscHashProgress progress, void* user, char out[41]) {
  out[0] = '\0';
  NodDiscOptions options{};
  options.preloader_threads = 2;
  NodHandle* disc = nullptr;
  if (nod_disc_open(path, &options, &disc) != NOD_RESULT_OK) {
    return PORT_DISC_HASH_READ_ERROR;
  }
  const uint64_t total = nod_disc_size(disc);
  constexpr size_t kChunk = 4u << 20;
  constexpr uint64_t kReportEvery = 16u << 20;
  std::vector<uint8_t> buffer(kChunk);
  Sha1Stream sha1;
  uint64_t done = 0, nextReport = 0;
  PortDiscHashResult result = PORT_DISC_HASH_OK;
  for (;;) {
    if (progress != nullptr && done >= nextReport) {
      if (!progress(done, total, user)) {
        result = PORT_DISC_HASH_STOPPED;
        break;
      }
      nextReport = done + kReportEvery;
    }
    const int64_t n = nod_read(disc, buffer.data(), buffer.size());
    if (n < 0) {
      result = PORT_DISC_HASH_READ_ERROR;
      break;
    }
    if (n == 0) {
      break;
    }
    sha1.Add(buffer.data(), static_cast<size_t>(n));
    done += static_cast<uint64_t>(n);
  }
  nod_free(disc);
  if (result == PORT_DISC_HASH_OK && total != 0 && done != total) {
    result = PORT_DISC_HASH_READ_ERROR;
  }
  if (result != PORT_DISC_HASH_OK) {
    return result;
  }
  if (progress != nullptr) {
    progress(done, total, user);
  }
  sha1.FinishHex(out);
  return PORT_DISC_HASH_OK;
}

}
