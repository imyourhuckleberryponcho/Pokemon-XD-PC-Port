#ifndef PORT_SAVESTATE_H
#define PORT_SAVESTATE_H

#include <any>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace portstate {
using Why = std::string;

struct Out {
  std::vector<uint8_t> b;
  void raw(const void* p, size_t n) {
    const uint8_t* c = static_cast<const uint8_t*>(p);
    b.insert(b.end(), c, c + n);
  }
  template <class T> void pod(const T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "pod");
    raw(&v, sizeof v);
  }
  void u64(uint64_t v) { pod(v); }
  void str(const std::string& s) { u64(s.size()); raw(s.data(), s.size()); }
  template <class T> void vec(const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable<T>::value, "pod");
    u64(v.size());
    raw(v.data(), v.size() * sizeof(T));
  }
};
struct In {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  bool bad = false;
  In(const uint8_t* data, size_t size) : p(data), n(size) {}
  bool ok() const { return !bad; }
  void raw(void* out, size_t k) {
    if (bad || pos + k > n) { bad = true; std::memset(out, 0, k); return; }
    std::memcpy(out, p + pos, k);
    pos += k;
  }
  template <class T> T pod() {
    static_assert(std::is_trivially_copyable<T>::value, "pod");
    alignas(T) unsigned char buf[sizeof(T)];
    raw(buf, sizeof buf);
    return *reinterpret_cast<T*>(buf);
  }
  uint64_t u64() { return pod<uint64_t>(); }
  std::string str() {
    const uint64_t k = u64();
    if (bad || k > n - pos) { bad = true; return {}; }
    std::string s(reinterpret_cast<const char*>(p + pos), static_cast<size_t>(k));
    pos += static_cast<size_t>(k);
    return s;
  }
  template <class T> std::vector<T> vec() {
    static_assert(std::is_trivially_copyable<T>::value, "pod");
    const uint64_t k = u64();
    std::vector<T> v;
    if (bad || k > (n - pos) / sizeof(T)) { bad = true; return v; }
    v.resize(static_cast<size_t>(k));
    std::memcpy(v.data(), p + pos, static_cast<size_t>(k) * sizeof(T));
    pos += static_cast<size_t>(k) * sizeof(T);
    return v;
  }
};
}

std::any PortState_OsSave(portstate::Why& why);
bool PortState_OsCheck(const std::any& state, portstate::Why& why);
void PortState_OsLoad(const std::any& state);
bool PortState_OsToBytes(const std::any& state, portstate::Out& out, portstate::Why& why);
bool PortState_OsFromBytes(portstate::In& in, std::any& state, portstate::Why& why);

std::any PortState_ViSave(portstate::Why& why);
bool PortState_ViCheck(const std::any& state, portstate::Why& why);
void PortState_ViLoad(const std::any& state);
void PortState_ViToBytes(const std::any& state, portstate::Out& out);
bool PortState_ViFromBytes(portstate::In& in, std::any& state);

bool PortState_IoIdle(portstate::Why& why);
std::any PortState_IoSave();
void PortState_IoLoad(const std::any& state);
void PortState_IoToBytes(const std::any& state, portstate::Out& out);
bool PortState_IoFromBytes(portstate::In& in, std::any& state);

std::any PortState_GxFifoSave(portstate::Why& why);
void PortState_GxFifoLoad(const std::any& state);
bool PortState_GxFifoToBytes(const std::any& state, portstate::Out& out, portstate::Why& why);
bool PortState_GxFifoFromBytes(portstate::In& in, std::any& state);

#endif
