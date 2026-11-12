#include <gx/fifo.hpp>

#include <cstdint>
#include <cstring>

extern "C" {
void PortGX_PipeU8(uint8_t v) { aurora::gx::fifo::write_u8(v); }
void PortGX_PipeU16(uint16_t v) { aurora::gx::fifo::write_u16(v); }
void PortGX_PipeU32(uint32_t v) { aurora::gx::fifo::write_u32(v); }
void PortGX_PipeU64(uint64_t v) { aurora::gx::fifo::write_u64(v); }
void PortGX_PipeF32(float v) { aurora::gx::fifo::write_f32(v); }
void PortGX_PipeF64(double v) {
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof(bits));
  aurora::gx::fifo::write_u64(bits);
}
}
