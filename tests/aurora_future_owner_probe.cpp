#include "aurora/aurora_browser_future_owner_accessor.h"
#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>
#include <cstring>

/* The candidate EM_JS functions are imports. These distinct native exports
 * call those imports so wasm-ld must retain and link the real ABI path. No
 * host store is seeded and no generated JavaScript is edited. */
extern "C" {
/*
 * The Emdawn port's $WebGPU library contains generated JS helpers whose
 * dependencies call native emwgpuCreate* functions. The port archive keeps
 * those definitions in webgpu.cpp.o, but a fixture with no native WebGPU
 * reference does not cause the archive member to be extracted. Taking the
 * address of this real public API creates the link edge without calling it,
 * creating an instance, or touching a browser host resource.
 */
using FutureOwnerWgpuCreateInstance =
    WGPUInstance (*)(WGPUInstanceDescriptor const*);
static FutureOwnerWgpuCreateInstance volatile
    future_owner_wgpu_create_instance_address = &wgpuCreateInstance;

EMSCRIPTEN_KEEPALIVE int future_owner_emdawn_link_anchor(void) {
  return future_owner_wgpu_create_instance_address != nullptr ? 1 : 0;
}

namespace {
enum RealWebGpuState : uint32_t {
  kStarted = 1u << 0,
  kAdapterRequested = 1u << 1,
  kAdapterReady = 1u << 2,
  kDeviceRequested = 1u << 3,
  kDeviceReady = 1u << 4,
  kQueueReady = 1u << 5,
  kSubmitted = 1u << 6,
  kWorkRequested = 1u << 7,
  kWorkCompleted = 1u << 8,
  kError = 1u << 9,
  kCallbackBeforeFuture = 1u << 10,
  kWorkSucceeded = 1u << 11,
  kCleanupRefused = 1u << 12,
  kCleanupStarted = 1u << 13,
  kCleanupComplete = 1u << 14,
  kDeviceLostObserved = 1u << 15,
  kUncapturedErrorObserved = 1u << 16,
  kLateCallback = 1u << 17,
  kDeviceLostExpected = 1u << 18,
  kObserverUnexpected = 1u << 19,
};

enum RealWebGpuLifecycle : uint32_t {
  kLifecycleIdle = 0,
  kLifecycleOpen = 1,
  kLifecycleClosing = 2,
  kLifecycleClosed = 3,
};

struct RealWebGpuFutureIds {
  uint64_t adapter;
  uint64_t device;
  uint64_t work;
};

struct RealWebGpuObserverDiagnostics {
  uint32_t device_lost_reason;
  uint32_t device_lost_callbacks;
  uint32_t uncaptured_error_callbacks;
  uint32_t unexpected_callbacks;
};

static WGPUInstance g_instance = nullptr;
static WGPUAdapter g_adapter = nullptr;
static WGPUDevice g_device = nullptr;
static WGPUQueue g_queue = nullptr;
static WGPUFuture g_adapter_future = WGPU_FUTURE_INIT;
static WGPUFuture g_device_future = WGPU_FUTURE_INIT;
static WGPUFuture g_work_future = WGPU_FUTURE_INIT;
static volatile uint32_t g_state = 0;
static volatile uint32_t g_adapter_future_published = 0;
static volatile uint32_t g_device_future_published = 0;
static volatile uint32_t g_work_future_published = 0;
static volatile uint32_t g_adapter_status = 0;
static volatile uint32_t g_device_status = 0;
static volatile uint32_t g_work_status = 0;
static volatile uint32_t g_lifecycle = kLifecycleIdle;
static volatile uint32_t g_pending_callbacks = 0;
static volatile uint32_t g_late_callback = 0;
static volatile uint32_t g_device_lost_reason = 0;
static volatile uint32_t g_device_lost_callbacks = 0;
static volatile uint32_t g_uncaptured_error_callbacks = 0;
static volatile uint32_t g_unexpected_callbacks = 0;
static RealWebGpuFutureIds g_future_ids{0, 0, 0};

void mark_error() { g_state |= kError; }

bool enter_pending_callback() {
  if (g_lifecycle != kLifecycleOpen) {
    g_late_callback = 1;
    g_state |= kLateCallback;
    return false;
  }
  if (g_pending_callbacks == 0) {
    mark_error();
    return false;
  }
  --g_pending_callbacks;
  return true;
}

bool reject_closed_callback() {
  if (g_lifecycle == kLifecycleClosed) {
    g_late_callback = 1;
    g_state |= kLateCallback;
    return false;
  }
  return true;
}

void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView,
                    void*, void*);
void on_uncaptured_error(WGPUDevice const*, WGPUErrorType, WGPUStringView,
                         void*, void*);

void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView,
                  void*, void*) {
  if (!enter_pending_callback()) return;
  g_work_status = static_cast<uint32_t>(status);
  if (!g_work_future_published) g_state |= kCallbackBeforeFuture;
  if (status == WGPUQueueWorkDoneStatus_Success) {
    g_state |= kWorkSucceeded;
  } else {
    mark_error();
  }
  g_state |= kWorkCompleted;
}

void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
               WGPUStringView, void*, void*) {
  if (!enter_pending_callback()) return;
  g_device_status = static_cast<uint32_t>(status);
  if (!g_device_future_published) g_state |= kCallbackBeforeFuture;
  if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
    mark_error();
    return;
  }
  g_device = device;
  g_state |= kDeviceReady;
}

void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                WGPUStringView, void*, void*) {
  if (!enter_pending_callback()) return;
  g_adapter_status = static_cast<uint32_t>(status);
  if (!g_adapter_future_published) g_state |= kCallbackBeforeFuture;
  if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
    mark_error();
    return;
  }
  g_adapter = adapter;
  g_state |= kAdapterReady;

  WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
  WGPUDeviceLostCallbackInfo lost_callback = WGPU_DEVICE_LOST_CALLBACK_INFO_INIT;
  lost_callback.mode = WGPUCallbackMode_AllowSpontaneous;
  lost_callback.callback = &on_device_lost;
  WGPUUncapturedErrorCallbackInfo error_callback = WGPU_UNCAPTURED_ERROR_CALLBACK_INFO_INIT;
  error_callback.callback = &on_uncaptured_error;
  descriptor.deviceLostCallbackInfo = lost_callback;
  descriptor.uncapturedErrorCallbackInfo = error_callback;
  WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = &on_device;
  g_state |= kDeviceRequested;
  ++g_pending_callbacks;
  g_device_future_published = 0;
  g_device_future = wgpuAdapterRequestDevice(g_adapter, &descriptor, callback);
  g_future_ids.device = g_device_future.id;
  g_device_future_published = 1;
}

void on_device_lost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                    WGPUStringView, void*, void*) {
  ++g_device_lost_callbacks;
  g_device_lost_reason = static_cast<uint32_t>(reason);
  const bool expected_destroy =
      g_lifecycle == kLifecycleClosing && reason == WGPUDeviceLostReason_Destroyed &&
      device != nullptr && *device == g_device && g_device != nullptr &&
      g_device_lost_callbacks == 1;
  if (expected_destroy) {
    g_state |= kDeviceLostObserved | kDeviceLostExpected;
    return;
  }
  if (!reject_closed_callback() || g_lifecycle != kLifecycleOpen) {
    g_late_callback = g_lifecycle == kLifecycleClosed ? 1u : g_late_callback;
    ++g_unexpected_callbacks;
    g_state |= kObserverUnexpected;
    mark_error();
    return;
  }
  ++g_unexpected_callbacks;
  g_state |= kObserverUnexpected;
  mark_error();
}

void on_uncaptured_error(WGPUDevice const*, WGPUErrorType,
                         WGPUStringView, void*, void*) {
  ++g_uncaptured_error_callbacks;
  if (!reject_closed_callback() || g_lifecycle != kLifecycleOpen) {
    ++g_unexpected_callbacks;
    g_state |= kObserverUnexpected;
    mark_error();
    return;
  }
  g_state |= kUncapturedErrorObserved;
  mark_error();
}

}  // namespace

EMSCRIPTEN_KEEPALIVE int future_owner_real_start(void) {
  if (g_lifecycle != kLifecycleIdle || g_instance != nullptr || g_state != 0) return 0;
  g_lifecycle = kLifecycleOpen;
  WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
  g_instance = wgpuCreateInstance(&descriptor);
  if (g_instance == nullptr) {
    mark_error();
    g_lifecycle = kLifecycleClosed;
    return 0;
  }
  g_state |= kStarted | kAdapterRequested;
  WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = &on_adapter;
  ++g_pending_callbacks;
  g_adapter_future_published = 0;
  g_adapter_future = wgpuInstanceRequestAdapter(g_instance, nullptr, callback);
  g_future_ids.adapter = g_adapter_future.id;
  g_adapter_future_published = 1;
  return 1;
}

EMSCRIPTEN_KEEPALIVE int future_owner_real_poll(void) {
  if (g_instance == nullptr) return 0;
  wgpuInstanceProcessEvents(g_instance);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int future_owner_real_submit(void) {
  if (g_device == nullptr || g_queue != nullptr || (g_state & kDeviceReady) == 0) return 0;
  g_queue = wgpuDeviceGetQueue(g_device);
  if (g_queue == nullptr) {
    mark_error();
    return 0;
  }
  g_state |= kQueueReady;
  WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(g_device, &encoder_descriptor);
  if (encoder == nullptr) {
    mark_error();
    return 0;
  }
  WGPUCommandBufferDescriptor buffer_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  WGPUCommandBuffer command_buffer =
      wgpuCommandEncoderFinish(encoder, &buffer_descriptor);
  if (command_buffer == nullptr) {
    wgpuCommandEncoderRelease(encoder);
    mark_error();
    return 0;
  }
  wgpuQueueSubmit(g_queue, 1, &command_buffer);
  wgpuCommandBufferRelease(command_buffer);
  wgpuCommandEncoderRelease(encoder);
  g_state |= kSubmitted | kWorkRequested;
  WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
  callback.mode = WGPUCallbackMode_AllowSpontaneous;
  callback.callback = &on_work_done;
  ++g_pending_callbacks;
  g_work_future_published = 0;
  g_work_future = wgpuQueueOnSubmittedWorkDone(g_queue, callback);
  g_future_ids.work = g_work_future.id;
  g_work_future_published = 1;
  return 1;
}

EMSCRIPTEN_KEEPALIVE uint32_t future_owner_real_state(void) {
  return g_state;
}

EMSCRIPTEN_KEEPALIVE size_t future_owner_real_ids_bytes(void) {
  return sizeof(RealWebGpuFutureIds);
}

EMSCRIPTEN_KEEPALIVE int future_owner_real_ids(void* out, size_t bytes) {
  if (out == nullptr || bytes < sizeof(g_future_ids)) return 0;
  std::memcpy(out, &g_future_ids, sizeof(g_future_ids));
  return 1;
}

EMSCRIPTEN_KEEPALIVE uint32_t future_owner_real_pending_callbacks(void) {
  return g_pending_callbacks;
}

EMSCRIPTEN_KEEPALIVE uint32_t future_owner_real_lifecycle(void) {
  return g_lifecycle;
}

EMSCRIPTEN_KEEPALIVE uint32_t future_owner_real_late_callback(void) {
  return g_late_callback;
}

EMSCRIPTEN_KEEPALIVE size_t future_owner_real_observer_bytes(void) {
  return sizeof(RealWebGpuObserverDiagnostics);
}

EMSCRIPTEN_KEEPALIVE int future_owner_real_observer(void* out, size_t bytes) {
  if (out == nullptr || bytes < sizeof(RealWebGpuObserverDiagnostics)) return 0;
  const RealWebGpuObserverDiagnostics diagnostics{
      g_device_lost_reason, g_device_lost_callbacks,
      g_uncaptured_error_callbacks, g_unexpected_callbacks};
  std::memcpy(out, &diagnostics, sizeof(diagnostics));
  return 1;
}

EMSCRIPTEN_KEEPALIVE int future_owner_real_cleanup(void) {
  if (g_lifecycle != kLifecycleOpen || g_pending_callbacks != 0 ||
      (g_state & kWorkSucceeded) == 0 || (g_state & kError) != 0 ||
      g_late_callback != 0 || g_unexpected_callbacks != 0) {
    g_state |= kCleanupRefused;
    return 0;
  }
  g_lifecycle = kLifecycleClosing;
  g_state |= kCleanupStarted;
  if (g_queue != nullptr) {
    wgpuQueueRelease(g_queue);
    g_queue = nullptr;
  }
  if (g_device != nullptr) {
    wgpuDeviceDestroy(g_device);
    wgpuDeviceRelease(g_device);
    g_device = nullptr;
  }
  if (g_adapter != nullptr) {
    wgpuAdapterRelease(g_adapter);
    g_adapter = nullptr;
  }
  if (g_instance != nullptr) {
    wgpuInstanceRelease(g_instance);
    g_instance = nullptr;
  }
  g_lifecycle = kLifecycleClosed;
  g_state |= kCleanupComplete;
  return 1;
}

EMSCRIPTEN_KEEPALIVE int future_owner_abi_fixture(void) { return 1; }
EMSCRIPTEN_KEEPALIVE int future_owner_capability(void) {
  return aurora_browser_future_owner_capability();
}
EMSCRIPTEN_KEEPALIVE int future_owner_summary(void* out, size_t bytes) {
  return aurora_browser_future_owner_summary(out, bytes);
}
EMSCRIPTEN_KEEPALIVE size_t future_owner_row_count(void) {
  return aurora_browser_future_owner_row_count();
}
EMSCRIPTEN_KEEPALIVE int future_owner_row(void* out, size_t bytes, int32_t ordinal) {
  return aurora_browser_future_owner_row(out, bytes, ordinal);
}
EMSCRIPTEN_KEEPALIVE int future_owner_dispose(void) {
  return aurora_browser_future_owner_dispose();
}
EMSCRIPTEN_KEEPALIVE size_t future_owner_summary_bytes(void) {
  return aurora_browser_future_owner_summary_bytes();
}
EMSCRIPTEN_KEEPALIVE size_t future_owner_row_bytes(void) {
  return aurora_browser_future_owner_row_bytes();
}
}

int main() { return 0; }
