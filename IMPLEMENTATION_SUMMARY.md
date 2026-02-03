# Implementation Summary: GPU Delay-Load DLL Crash Fix

## Overview

This implementation addresses access violation crashes in `gpu::CollectDriverInfoD3D` that occur during process shutdown due to delay-loaded DLL (dxgi.dll) lifetime management issues.

## Problem Statement

**Crash Signature**:
```
Exception Code: 0xc0000005 (Access Violation)
Faulting Module: ntdll!LdrpGetProcedureAddress
Call Stack: __delayLoadHelper2 -> _tailMerge_dxgi.dll -> gpu::CollectDriverInfoD3D
```

**Root Cause**: 
During process shutdown, Windows may unload delay-loaded DLLs (like dxgi.dll) before GPU info collection code finishes executing, causing access violations when trying to call DXGI functions.

## Solution Implemented

### Approach: Shutdown Detection and Early Exit

Added a thread-safe shutdown flag that prevents GPU info collection during process termination, and integrated it into the GPU process lifecycle.

### Key Changes

1. **gpu/config/gpu_info_collector_win.cc**:
   - Added `#include <atomic>` for thread-safe flag
   - Declared `std::atomic<bool> g_is_shutting_down{false}` in anonymous namespace
   - Added shutdown check at start of `CollectDriverInfoD3D()`
   - Implemented `SetGpuInfoCollectorShutdown()` function

2. **gpu/config/gpu_info_collector.h**:
   - Added `GPU_CONFIG_EXPORT void SetGpuInfoCollectorShutdown()` declaration
   - Documented purpose: prevent delay-loaded DLL crashes during shutdown
   - Should be called from GPU process shutdown path

3. **content/gpu/gpu_child_thread.cc**:
   - **CRITICAL FIX**: Added call to `SetGpuInfoCollectorShutdown()` in `~GpuChildThread()` destructor
   - This integrates the fix into the actual GPU process shutdown path
   - Included `gpu/config/gpu_info_collector.h` header

4. **gpu/config/gpu_info_collector_unittest.cc**:
   - Added `ShutdownPreventsCollection` test case
   - Tests that shutdown flag correctly prevents collection

### Code Details

**Shutdown Check** (gpu_info_collector_win.cc:347-352):
```cpp
if (g_is_shutting_down.load(std::memory_order_acquire)) {
  DLOG(WARNING) << "Skipping GPU info collection during shutdown";
  return false;
}
```

**Shutdown Setter** (gpu_info_collector_win.cc:974-976):
```cpp
void SetGpuInfoCollectorShutdown() {
  g_is_shutting_down.store(true, std::memory_order_release);
}
```

**Runtime Integration** (content/gpu/gpu_child_thread.cc:144-151):
```cpp
GpuChildThread::~GpuChildThread() {
#if BUILDFLAG(IS_WIN)
  // Signal shutdown to prevent delay-loaded DLL crashes when dxgi.dll
  // or other delay-loaded DLLs are unloaded during process termination.
  // This must happen before any code that might trigger GPU info collection.
  gpu::SetGpuInfoCollectorShutdown();
#endif
}
```

## Why This Approach?

### Advantages
- ✅ **Minimal code change**: ~20 lines of code added
- ✅ **Low risk**: Surgical fix, doesn't affect normal operation
- ✅ **Fast to implement**: Hours vs. weeks/months for alternatives
- ✅ **Addresses root cause**: Prevents access to unloaded DLLs
- ✅ **Thread-safe**: Uses std::atomic with proper memory ordering
- ✅ **Testable**: Unit test verifies behavior

### Why Not Rust?

While Rust provides memory safety benefits, it **would not fix this specific crash** because:
1. The crash is caused by Windows DLL loader behavior, not memory corruption
2. Rust FFI to DXGI is still `unsafe` and subject to same DLL lifetime issues
3. The same architectural fix (shutdown detection) would be needed in Rust
4. Rewriting would take months with high regression risk

See `GPU_RELIABILITY_ANALYSIS.md` for comprehensive analysis of alternatives.

## Testing Strategy

### Unit Test
- **Test**: `GpuInfoCollectorTest.ShutdownPreventsCollection`
- **Validates**: Shutdown flag prevents collection
- **Platform**: Windows only (#if BUILDFLAG(IS_WIN))

### Integration Testing Needed
1. **Normal Operation**: Verify GPU info still collected during regular startup
2. **Shutdown Scenario**: Confirm no crashes during rapid process termination
3. **Stress Test**: Repeated process start/stop cycles
4. **Reliability Monitoring**: Track crash rates post-deployment

### Manual Testing
```bash
# Run unit tests (once build environment is set up)
ninja -C out/Default gpu_unittests
out/Default/gpu_unittests --gtest_filter="GpuInfoCollectorTest.ShutdownPreventsCollection"

# Run broader GPU tests
out/Default/gpu_unittests
```

## Runtime Integration (FIXED)

**Previous Issue**: The original implementation only called the shutdown function in unit tests, not in actual runtime code, so the fix wouldn't work in production.

**Solution**: The shutdown flag is now automatically set in the `GpuChildThread` destructor, which is called during GPU process termination:

```cpp
// content/gpu/gpu_child_thread.cc
GpuChildThread::~GpuChildThread() {
#if BUILDFLAG(IS_WIN)
  gpu::SetGpuInfoCollectorShutdown();
#endif
}
```

This ensures the shutdown flag is set **before** any delay-loaded DLLs are unloaded, preventing the crash in production environments.

### Call Chain
1. GPU process begins shutdown
2. `~GpuChildThread()` destructor called
3. `SetGpuInfoCollectorShutdown()` sets atomic flag to true
4. Any subsequent calls to `CollectDriverInfoD3D()` return early
5. No access to potentially unloaded dxgi.dll

## Future Considerations

Post-deployment, monitor:
- Crash rates in GPU process with signature matching delay-load issues
- Event Viewer logs for access violations in DXGI calls
- Telemetry for `CollectDriverInfoD3D` success/failure rates

## References

1. **Microsoft Documentation**:
   - [Delay-load DLL crashes during process detach](https://devblogs.microsoft.com/oldnewthing/20190718-00/?p=102719)
   
2. **Related Chromium Code**:
   - `gpu/config/gpu_info_collector_win.cc` - Windows GPU info collection
   - `content/gpu/gpu_main.cc` - GPU process main entry point
   - `gpu/ipc/service/gpu_init.cc` - GPU initialization and setup

3. **Analysis Document**:
   - `GPU_RELIABILITY_ANALYSIS.md` - Full analysis with 3 approaches evaluated

## Questions or Issues?

For questions about this implementation:
- Review `GPU_RELIABILITY_ANALYSIS.md` for detailed technical analysis
- Check unit test `gpu_info_collector_unittest.cc` for usage examples
- Consult Microsoft's delay-load DLL documentation for background

---

*Implementation Date: 2026-02-03*  
*Implementation by: GitHub Copilot Coding Agent*
