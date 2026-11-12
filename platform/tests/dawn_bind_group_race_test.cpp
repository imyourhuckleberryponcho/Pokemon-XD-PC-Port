#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "webgpu/sampler_keeper.hpp"

namespace {

constexpr int Slots = 8;
std::atomic<uint64_t> g_created{0};
std::atomic<uint64_t> g_released{0};
std::atomic<bool> g_stop{false};

wgpu::BackendType backend_from(const char* name) {
  if (std::strcmp(name, "d3d11") == 0) {
    return wgpu::BackendType::D3D11;
  }
  if (std::strcmp(name, "vulkan") == 0) {
    return wgpu::BackendType::Vulkan;
  }
  return wgpu::BackendType::D3D12;
}

}

int main(int argc, char** argv) {
  const bool keeper = argc > 1 && std::strcmp(argv[1], "keeper") == 0;
  const wgpu::BackendType backend = backend_from(argc > 2 ? argv[2] : "d3d12");
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 20;

  constexpr std::array instanceFeatures{wgpu::InstanceFeatureName::TimedWaitAny};
  const wgpu::InstanceDescriptor instanceDescriptor{
      .requiredFeatureCount = instanceFeatures.size(),
      .requiredFeatures = instanceFeatures.data(),
  };
  wgpu::Instance instance = wgpu::CreateInstance(&instanceDescriptor);
  wgpu::Adapter adapter;
  const wgpu::RequestAdapterOptions adapterOptions{.backendType = backend};
  instance.WaitAny(instance.RequestAdapter(&adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::RequestAdapterStatus, wgpu::Adapter a, wgpu::StringView) {
                                             adapter = std::move(a);
                                           }),
                   UINT64_MAX);
  if (!adapter) {
    std::printf("no adapter for this backend\n");
    return 1;
  }
  const std::array features{wgpu::FeatureName::ImplicitDeviceSynchronization};
  wgpu::DeviceDescriptor deviceDescriptor{};
  deviceDescriptor.requiredFeatureCount = features.size();
  deviceDescriptor.requiredFeatures = features.data();
  deviceDescriptor.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
    std::printf("WebGPU error: %.*s\n", static_cast<int>(message.length), message.data);
    std::fflush(stdout);
    std::_Exit(3);
  });
  wgpu::Device device;
  instance.WaitAny(adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
                                         [&](wgpu::RequestDeviceStatus, wgpu::Device d, wgpu::StringView) {
                                           device = std::move(d);
                                         }),
                   UINT64_MAX);
  if (!device) {
    std::printf("no device\n");
    return 1;
  }
  wgpu::AdapterInfo info;
  adapter.GetInfo(&info);
  std::printf("mode %s, adapter %.*s, backend %d, %d s\n", keeper ? "keeper" : "plain",
              static_cast<int>(info.device.length), info.device.data, static_cast<int>(info.backendType), seconds);
  std::fflush(stdout);

  std::array<wgpu::BindGroupLayoutEntry, Slots * 2> layoutEntries{};
  for (int i = 0; i < Slots; ++i) {
    layoutEntries[i * 2] = {.binding = static_cast<uint32_t>(i * 2),
                            .visibility = wgpu::ShaderStage::Fragment,
                            .texture = {.sampleType = wgpu::TextureSampleType::Float}};
    layoutEntries[i * 2 + 1] = {.binding = static_cast<uint32_t>(i * 2 + 1),
                                .visibility = wgpu::ShaderStage::Fragment,
                                .sampler = {.type = wgpu::SamplerBindingType::Filtering}};
  }
  const wgpu::BindGroupLayoutDescriptor layoutDescriptor{.entryCount = layoutEntries.size(),
                                                         .entries = layoutEntries.data()};
  const wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&layoutDescriptor);
  const wgpu::TextureDescriptor textureDescriptor{
      .usage = wgpu::TextureUsage::TextureBinding, .size = {1, 1}, .format = wgpu::TextureFormat::RGBA8Unorm};
  const wgpu::TextureView view = device.CreateTexture(&textureDescriptor).CreateView();
  std::vector<wgpu::Sampler> samplers;
  for (int i = 0; i < 4; ++i) {
    const wgpu::SamplerDescriptor samplerDescriptor{.lodMaxClamp = 1.0f + static_cast<float>(i)};
    samplers.push_back(device.CreateSampler(&samplerDescriptor));
  }

  std::mutex queueMutex;
  std::deque<wgpu::BindGroup> queue;
  auto creator = [&](unsigned seed) {
    std::mt19937 rng{seed};
    while (!g_stop.load(std::memory_order_relaxed)) {
      std::array<wgpu::BindGroupEntry, Slots * 2> entries{};
      for (int i = 0; i < Slots; ++i) {
        entries[i * 2] = {.binding = static_cast<uint32_t>(i * 2), .textureView = view};
        entries[i * 2 + 1] = {.binding = static_cast<uint32_t>(i * 2 + 1),
                              .sampler = samplers[i < 2 ? rng() % samplers.size() : 0]};
      }
      const wgpu::BindGroupDescriptor descriptor{
          .layout = layout, .entryCount = entries.size(), .entries = entries.data()};
      wgpu::BindGroup bindGroup = keeper ? aurora::webgpu::create_bind_group(device, descriptor)
                                         : device.CreateBindGroup(&descriptor);
      g_created.fetch_add(1, std::memory_order_relaxed);
      std::lock_guard lock{queueMutex};
      queue.push_back(std::move(bindGroup));
    }
  };
  auto releaser = [&] {
    while (!g_stop.load(std::memory_order_relaxed)) {
      wgpu::BindGroup bindGroup;
      {
        std::lock_guard lock{queueMutex};
        if (!queue.empty()) {
          bindGroup = std::move(queue.front());
          queue.pop_front();
        }
      }
      if (bindGroup) {
        bindGroup = nullptr;
        g_released.fetch_add(1, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
  };
  std::vector<std::thread> threads;
  threads.emplace_back(creator, 1u);
  threads.emplace_back(creator, 2u);
  threads.emplace_back(releaser);
  threads.emplace_back(releaser);

  const auto start = std::chrono::steady_clock::now();
  auto lastChange = start;
  uint64_t last = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto now = std::chrono::steady_clock::now();
    const uint64_t created = g_created.load();
    if (created != last) {
      last = created;
      lastChange = now;
    }
    if (now - lastChange > std::chrono::seconds(5)) {
      std::printf("STUCK: no bind group created for 5 s (after %llu created, %llu released)\n",
                  static_cast<unsigned long long>(created), static_cast<unsigned long long>(g_released.load()));
      std::fflush(stdout);
      std::_Exit(2);
    }
    if (now - start > std::chrono::seconds(seconds)) {
      break;
    }
  }
  g_stop = true;
  for (auto& thread : threads) {
    thread.join();
  }
  queue.clear();
  std::printf("OK: %llu bind groups created and released, %zu sampler sets kept\n",
              static_cast<unsigned long long>(g_created.load()), aurora::webgpu::sampler_keeper_count());
  aurora::webgpu::shutdown_sampler_keeper();
  return 0;
}
