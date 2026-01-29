# Dawn vs wgpu.rs: WebGPU Runtime Engine Analysis

**Document Status:** Analysis Report  
**Author:** Technical Analysis Team  
**Date:** 2026-01-29  
**Purpose:** Analyze discrepancies between Dawn (C++) and wgpu.rs (Rust) WebGPU runtime engines and explore making the runtime switchable.

---

## Executive Summary

This document provides a comprehensive analysis of Chromium's current Dawn WebGPU implementation (C++) versus the wgpu.rs alternative (Rust), addresses why the Rust alternative is not necessarily "better" in all contexts, and proposes a strategy for making the runtime engine switchable via feature flags.

**Key Findings:**
- Dawn is deeply integrated into Chromium's architecture with mature tooling
- wgpu.rs offers memory safety advantages but faces integration challenges
- A switchable runtime approach is technically feasible but requires significant architectural work
- Neither solution is universally "better" - each has distinct tradeoffs

---

## Table of Contents

1. [Background](#background)
2. [Architecture Comparison](#architecture-comparison)
3. [Memory Safety Analysis](#memory-safety-analysis)
4. [Performance Considerations](#performance-considerations)
5. [Integration Challenges](#integration-challenges)
6. [Why wgpu.rs is Not Objectively "Better"](#why-wgpurs-is-not-objectively-better)
7. [Feature Flag Implementation Strategy](#feature-flag-implementation-strategy)
8. [Recommendations](#recommendations)

---

## Background

### WebGPU Overview

WebGPU is the successor to WebGL, providing modern GPU access for web applications with features like:
- GPU compute capabilities
- Lower overhead than WebGL
- Better predictable performance
- Modern graphics API abstraction (Vulkan, Metal, Direct3D)

### Dawn WebGPU Implementation

**Dawn** is Google's C++ implementation of the WebGPU specification, consisting of:

1. **Dawn Wire**: Client-server serialization layer
   - **Dawn Wire Client**: Runs in renderer process, serializes WebGPU API calls
   - **Dawn Wire Server**: Runs in GPU process, deserializes commands
   
2. **Dawn Native**: Native WebGPU implementation
   - Wraps platform-specific GPU APIs (Vulkan, Metal, D3D12)
   - Handles resource management, command recording, validation

**Architecture Flow:**
```
JavaScript (Blink)
    ↓
Dawn Wire Client (Renderer Process)
    ↓ IPC (WebGPUDecoderImpl)
Dawn Wire Server (GPU Process)
    ↓
Dawn Native
    ↓
Platform GPU API (Vulkan/Metal/D3D12)
```

### wgpu.rs Implementation

**wgpu.rs** is Mozilla's Rust implementation of WebGPU, featuring:
- Written in memory-safe Rust
- Powers Firefox's WebGPU implementation
- Also uses wgpu-core for the native implementation
- Has its own intermediate representation layer

**Key Characteristics:**
- Memory safety by design through Rust's ownership system
- Active development by Mozilla's graphics team
- Cross-platform support similar to Dawn
- Different architecture and design philosophy

---

## Architecture Comparison

### Dawn Architecture

**Strengths:**
1. **Deep Chromium Integration**
   - Built specifically for Chrome's multi-process architecture
   - Tight integration with Chrome's IPC mechanisms
   - Optimized for Chrome's GPU Command Buffer infrastructure
   - Extensive use of Oilpan (Blink's garbage collector) for resource management

2. **Mature Tooling**
   - Well-integrated with Chrome's build system (GN)
   - Comprehensive testing infrastructure (ClusterFuzz)
   - Established debugging workflows
   - Extensive documentation and institutional knowledge

3. **Platform Coverage**
   - Supports all Chromium platforms (Windows, macOS, Linux, ChromeOS, Android)
   - Optimized backends for each platform
   - SwiftShader fallback for software rendering

**Weaknesses:**
1. **Memory Safety**
   - Written in C++, susceptible to memory safety issues
   - Use-after-free vulnerabilities documented in security reviews
   - Raw pointer usage patterns that can break with code changes
   - Reference counting complexity with manual management

2. **Code Complexity**
   - Multiple layers of abstraction
   - Complex state tracking across process boundaries
   - Challenging to maintain and audit for security

### wgpu.rs Architecture

**Strengths:**
1. **Memory Safety**
   - Rust's ownership system prevents entire classes of vulnerabilities
   - No use-after-free, double-free, or dangling pointer issues by design
   - Compiler-enforced memory safety guarantees
   - Reduced security audit surface

2. **Modern Design**
   - Clean API design from the ground up
   - Simpler resource lifetime management
   - More maintainable codebase

3. **Cross-Browser Support**
   - Used in Firefox, providing cross-browser validation
   - Benefits from multi-implementer perspective

**Weaknesses:**
1. **Integration Complexity**
   - Not designed specifically for Chromium's architecture
   - Would require significant adapter layer
   - Different assumptions about process model
   - Foreign Function Interface (FFI) overhead for C++ interop

2. **Chromium Ecosystem Gap**
   - Less mature integration with Chrome's infrastructure
   - Different build system expectations (Cargo vs GN)
   - Testing infrastructure would need adaptation
   - Developer tooling less mature in Chrome context

3. **Performance Overhead**
   - FFI boundary crossings add overhead
   - Potential serialization/deserialization costs
   - May not be as optimized for Chrome's specific use cases

---

## Memory Safety Analysis

### Dawn Memory Safety Issues

Based on Chromium's security review (docs/security/research/graphics/webgpu_technical_report.md):

**1. Reference Counting Vulnerabilities**
```
Issue: Raw pointer usage in Dawn Native without proper reference counting
Impact: Use-after-free vulnerabilities
Example: GPUCommandBuffer objects holding raw pointers to resources
```

**Specific Patterns:**
- Dawn Native objects hold raw pointers to reference-counted objects
- Assumption that references are held elsewhere can break
- Complex object lifetime management across IPC boundaries
- Multiple objects holding references to the same resource

**2. Shared Memory Handling**
- Dawn Wire operates on shared memory between renderer and GPU process
- Potential TOCTOU (Time-of-check-time-of-use) vulnerabilities
- Requires careful validation and copying of shared memory data

**3. State Tracking Complexity**
- Long chains of reference-counted objects across processes
- Oilpan (Blink) → Dawn Wire Client → Dawn Wire Server → Dawn Native → Vulkan/etc
- Garbage collection in renderer can affect GPU process state

### wgpu.rs Memory Safety Advantages

**Rust Ownership System:**
```rust
// Example: Ownership prevents use-after-free
fn process_buffer(buffer: Buffer) {
    // buffer is owned here
    use_buffer(&buffer);
} // buffer automatically cleaned up, no manual tracking needed
```

**Key Safety Features:**
1. **Compile-time Guarantees**
   - Ownership and borrowing prevent data races
   - Lifetime annotations ensure references stay valid
   - No null pointer dereferences

2. **Automatic Memory Management**
   - RAII (Resource Acquisition Is Initialization) pattern
   - Drop trait ensures cleanup
   - No manual reference counting needed

3. **Thread Safety**
   - Send and Sync traits ensure safe concurrency
   - Prevents data races at compile time

**Limitations:**
- Unsafe blocks still possible (but auditable and isolated)
- FFI boundaries to C++ code are inherently unsafe
- Logic errors still possible (memory safety ≠ correctness)

### Comparative Risk Assessment

| Risk Category | Dawn (C++) | wgpu.rs (Rust) |
|--------------|------------|----------------|
| Use-after-free | High | Very Low |
| Double-free | Medium | Very Low |
| Buffer overflow | Medium | Low |
| Data races | Medium | Very Low |
| Logic errors | Medium | Medium |
| FFI vulnerabilities | N/A | Medium-High |

---

## Performance Considerations

### Dawn Performance Profile

**Advantages:**
1. **Direct Integration**
   - No FFI overhead within Dawn stack
   - Optimized for Chrome's specific workloads
   - Years of performance tuning for Chrome use cases

2. **Zero-Copy Operations**
   - Direct memory access in GPU process
   - Optimized shared memory handling
   - Minimal serialization overhead within Dawn

3. **Platform Optimizations**
   - Backend-specific optimizations (D3D12, Metal, Vulkan)
   - Tuned for each platform's quirks
   - Chrome-specific fast paths

**Performance Costs:**
1. Manual memory management overhead
2. Reference counting atomic operations
3. Validation overhead to prevent security issues

### wgpu.rs Performance Profile

**Advantages:**
1. **Zero-Cost Abstractions**
   - Rust's zero-cost abstractions philosophy
   - No garbage collection pauses
   - Efficient memory layouts

2. **Optimized Compilation**
   - LLVM backend (same as Dawn/C++)
   - Link-time optimization opportunities
   - Modern compiler optimizations

**Performance Costs:**
1. **FFI Overhead**
   - Crossing C++/Rust boundary adds latency
   - Data marshaling costs
   - Cannot use Rust types directly in C++ code

2. **Integration Adapter Layer**
   - Additional translation layer needed
   - Potential double-validation
   - Serialization/deserialization at FFI boundary

3. **Unoptimized for Chrome**
   - Not tuned for Chrome's specific patterns
   - Would require significant optimization work

### Benchmarking Considerations

Without actual implementation, concrete performance comparison is speculative. Key factors:
- FFI overhead typically 1-10 nanoseconds per call
- For GPU-bound workloads, this may be negligible
- For CPU-bound validation, could be significant
- Would need real-world testing to determine impact

---

## Integration Challenges

### Technical Challenges

**1. Build System Integration**

**Current State (Dawn):**
```gn
# Dawn uses Chrome's GN build system
source_set("dawn_native") {
  sources = [...]
  deps = [
    "//base",
    "//gpu/command_buffer/common",
  ]
}
```

**Required for wgpu.rs:**
```gn
# Would need to integrate Cargo build
rust_static_library("wgpu_core") {
  crate_root = "wgpu/wgpu-core/src/lib.rs"
  sources = [...]
  deps = [
    "//build/rust:std",
    "//third_party/rust/...",
  ]
}
```

**Challenges:**
- Cargo (Rust's build system) vs GN integration
- Dependency management (crates.io vs Chrome's DEPS)
- Cross-compilation support for all Chrome platforms
- Build time considerations (Rust compilation can be slower)

**2. IPC and Process Architecture**

Dawn's architecture is tightly coupled to Chrome's multi-process model:

```
Current (Dawn):
Blink (C++) → Dawn Wire Client (C++) → IPC → Dawn Wire Server (C++) → Dawn Native (C++)
                                       ↑
                                  Chrome's GPU Command Buffer
```

```
With wgpu.rs:
Blink (C++) → Adapter (C++) → wgpu FFI (C++) ↔ wgpu-core (Rust) → Platform API
                              ↑
                         FFI Boundary
```

**Challenges:**
- wgpu.rs not designed for Chrome's multi-process architecture
- Would need to build Dawn Wire equivalent for wgpu
- Or build FFI adapter layer (significant overhead)
- Process sandboxing considerations

**3. API Surface Adaptation**

Dawn provides C++ APIs consumed by Blink:
```cpp
// Dawn API (C++)
WGPUDevice CreateDevice(const WGPUDeviceDescriptor* descriptor);
WGPUBuffer CreateBuffer(const WGPUBufferDescriptor* descriptor);
```

wgpu.rs provides Rust APIs:
```rust
// wgpu API (Rust)
impl Device {
    pub fn create_buffer(&self, desc: &BufferDescriptor) -> Buffer;
}
```

**Required:**
- C-compatible FFI layer for wgpu
- Data structure conversion between C++ and Rust
- Lifetime management across FFI boundary
- Error handling translation

**4. Platform Backend Support**

| Platform | Dawn Support | wgpu.rs Support | Integration Effort |
|----------|--------------|-----------------|-------------------|
| Windows (D3D12) | Mature | Good | Medium |
| macOS (Metal) | Mature | Good | Medium |
| Linux (Vulkan) | Mature | Good | Low |
| Android (Vulkan) | Mature | Limited | High |
| ChromeOS | Mature | Unknown | High |
| iOS | Partial | Good | High |

**5. Testing Infrastructure**

Dawn benefits from:
- ClusterFuzz integration
- Chrome's GPU testing infrastructure
- Platform-specific test bots
- WebGPU CTS (Conformance Test Suite)

wgpu.rs would need:
- Adaptation of existing test infrastructure
- New fuzzing harnesses for FFI layer
- Platform test coverage validation
- Performance regression testing

### Development and Maintenance Challenges

**1. Team Expertise**
- Chrome team has deep Dawn expertise
- Limited wgpu.rs expertise in Chrome team
- Mozilla team maintains wgpu.rs, but for Firefox
- Knowledge transfer and training required

**2. Debugging and Tooling**
- Chrome DevTools integration for Dawn
- Debugging across C++/Rust boundary is challenging
- Different mental models and tools

**3. Long-term Maintenance**
- Two codebases to maintain (if switchable)
- Keeping both implementations in sync with WebGPU spec
- Testing matrix explosion (2x platforms × tests)

---

## Why wgpu.rs is Not Objectively "Better"

### The Memory Safety Tradeoff

**Reality:** Memory safety is one important factor, but not the only one.

**1. Security is Multi-faceted**

Even with memory safety, wgpu.rs integration could introduce:
- **FFI vulnerabilities**: The C++/Rust boundary becomes a critical attack surface
- **Logic vulnerabilities**: Rust prevents memory bugs, not logic errors
- **Integration bugs**: New adapter layer introduces new complexity
- **Supply chain risks**: Additional dependencies from crates.io ecosystem

**2. Performance Matters**

For a browser's graphics stack:
- Rendering performance directly affects user experience
- Frame drops cause visible jank
- GPU driver interactions are performance-critical
- Adding overhead (FFI, adapter layers) has real costs

**3. Integration Complexity**

wgpu.rs was designed for Firefox, not Chrome:
- Different architectural assumptions
- Different process models
- Different API expectations
- Would require substantial adaptation work

### Why Dawn is Currently Better for Chrome

**1. Proven in Production**
- Shipped in Chrome since M113 (2023)
- Battle-tested with millions of users
- Known performance characteristics
- Understood security properties (even if not perfect)

**2. Optimized for Chrome**
- Built specifically for Chrome's architecture
- Tight integration with Chrome's infrastructure
- Optimized for Chrome's workloads
- Minimal overhead

**3. Team Efficiency**
- Chrome team is expert in Dawn
- Established workflows and tools
- Can fix issues quickly
- Institutional knowledge

**4. Lower Risk**
- Incremental security improvements possible
- No need to rewrite entire stack
- Can add Rust components gradually where most beneficial
- Avoids big-bang migration risk

### When wgpu.rs Might Be Better

**Hypothetical Scenarios:**

1. **Starting from Scratch**
   - If building WebGPU implementation today
   - Without existing Dawn investment
   - Then Rust might be preferred

2. **Firefox Context**
   - wgpu.rs is better for Firefox
   - Designed for their architecture
   - Leverages their Rust expertise

3. **Long-term Vision**
   - If memory safety becomes paramount
   - If Chrome significantly increases Rust adoption
   - If integration challenges can be fully solved

---

## Feature Flag Implementation Strategy

### Goal

Enable runtime selection between Dawn (C++) and wgpu.rs (Rust) WebGPU implementations to:
1. Allow controlled testing of wgpu.rs in Chrome
2. Enable gradual migration if desired
3. Provide fallback mechanism
4. Support experimentation and comparison

### High-Level Design

```
                          ┌─────────────────┐
                          │  Feature Flag   │
                          │   (GN + Runtime)│
                          └────────┬────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
            ┌───────▼──────┐            ┌────────▼──────┐
            │  Dawn Path   │            │  wgpu.rs Path │
            │   (Default)  │            │ (Experimental)│
            └───────┬──────┘            └────────┬──────┘
                    │                            │
            ┌───────▼──────────┐        ┌────────▼──────────┐
            │ Dawn Wire Client │        │ wgpu FFI Adapter  │
            └───────┬──────────┘        └────────┬──────────┘
                    │                            │
            ┌───────▼──────────┐        ┌────────▼──────────┐
            │ Dawn Wire Server │        │  wgpu-core (Rust) │
            └───────┬──────────┘        └────────┬──────────┘
                    │                            │
            ┌───────▼──────────┐        ┌────────▼──────────┐
            │   Dawn Native    │        │  wgpu-hal (Rust)  │
            └───────┬──────────┘        └────────┬──────────┘
                    │                            │
                    └──────────┬─────────────────┘
                               │
                        ┌──────▼──────┐
                        │  Platform   │
                        │  GPU API    │
                        │ (Vulkan/etc)│
                        └─────────────┘
```

### Implementation Phases

#### Phase 1: Build Infrastructure (3-6 months)

**Objective:** Enable building both Dawn and wgpu.rs in Chromium

**Tasks:**
1. **Add wgpu.rs to third_party/**
   ```python
   # In DEPS file
   'src/third_party/wgpu': {
     'url': 'https://github.com/gfx-rs/wgpu.git@{wgpu_revision}',
   }
   ```

2. **Create GN build rules for wgpu**
   ```gn
   # //third_party/wgpu/BUILD.gn
   rust_static_library("wgpu_core") {
     crate_root = "wgpu-core/src/lib.rs"
     sources = [ ... ]
     deps = [
       ":wgpu_hal",
       "//third_party/rust/...",
     ]
     features = [
       "vulkan",
       "metal",
       "dx12",
     ]
   }
   
   rust_static_library("wgpu_hal") {
     crate_root = "wgpu-hal/src/lib.rs"
     # Platform-specific features
   }
   ```

3. **Add build flag**
   ```gn
   # //gpu/BUILD.gn
   declare_args() {
     # Enable experimental wgpu.rs backend
     use_wgpu_rust_backend = false
   }
   ```

4. **Platform-specific configuration**
   - Ensure Rust toolchain available for all platforms
   - Configure platform-specific features
   - Test compilation on all builder bots

**Deliverables:**
- wgpu.rs compiles in Chrome build
- No runtime integration yet
- Build flag to enable/disable

#### Phase 2: FFI Adapter Layer (6-9 months)

**Objective:** Create C++ adapter layer to expose wgpu.rs with Dawn-compatible API

**Architecture:**
```cpp
// //gpu/webgpu/wgpu_adapter/wgpu_adapter.h
#ifndef GPU_WEBGPU_WGPU_ADAPTER_H_
#define GPU_WEBGPU_WGPU_ADAPTER_H_

#include <webgpu/webgpu.h>  // WebGPU C header
#include "gpu/webgpu/wgpu_ffi.h"  // Rust FFI declarations

namespace gpu {
namespace webgpu {

// Adapter that implements WebGPU C API using wgpu.rs
class WGPUAdapter {
 public:
  // Implement WebGPU C API functions
  WGPUDevice CreateDevice(const WGPUDeviceDescriptor* descriptor);
  WGPUBuffer CreateBuffer(const WGPUBufferDescriptor* descriptor);
  // ... other WebGPU APIs
  
 private:
  // Handle to Rust wgpu instance
  WGPURustInstance* rust_instance_;
};

}  // namespace webgpu
}  // namespace gpu

#endif  // GPU_WEBGPU_WGPU_ADAPTER_H_
```

```rust
// //third_party/wgpu/chromium_ffi/src/lib.rs
use wgpu_core::{device, instance, adapter};

#[repr(C)]
pub struct WGPURustInstance {
    // Opaque handle to Rust-side instance
}

#[no_mangle]
pub extern "C" fn wgpu_create_instance() -> *mut WGPURustInstance {
    let instance = instance::Instance::new();
    Box::into_raw(Box::new(instance)) as *mut WGPURustInstance
}

#[no_mangle]
pub extern "C" fn wgpu_instance_create_device(
    instance: *mut WGPURustInstance,
    descriptor: *const WGPUDeviceDescriptor,
) -> *mut WGPURustDevice {
    // Convert C descriptor to Rust types
    // Create device
    // Return opaque handle
}

// ... more FFI functions
```

**Challenges:**
1. **Lifetime Management**
   - C++ manages object lifetimes with ref-counting
   - Rust uses ownership
   - Need careful handle management across boundary

2. **Error Handling**
   - C++ uses error callbacks and return codes
   - Rust uses Result<T, E>
   - Need translation layer

3. **Async Operations**
   - WebGPU has many async operations
   - Need to bridge Rust futures to C++ callbacks

4. **Thread Safety**
   - Ensure thread-safe access across FFI
   - Handle Chrome's multi-thread architecture

**Deliverables:**
- Complete FFI adapter layer
- Unit tests for adapter
- Performance benchmarks

#### Phase 3: Integration Layer (6-9 months)

**Objective:** Integrate wgpu adapter into Chrome's WebGPU stack

**Tasks:**

1. **Create Backend Selection Layer**
   ```cpp
   // //gpu/command_buffer/service/webgpu_backend.h
   namespace gpu {
   namespace webgpu {
   
   enum class WebGPUBackend {
     kDawn,    // Default C++ Dawn
     kWGPU,    // Experimental Rust wgpu.rs
   };
   
   class WebGPUBackendFactory {
    public:
     static WebGPUBackend GetBackend();
     static void SetBackend(WebGPUBackend backend);
     
     static std::unique_ptr<WebGPUInterface> CreateBackend();
   };
   
   }  // namespace webgpu
   }  // namespace gpu
   ```

2. **Implement Backend Interface**
   ```cpp
   // //gpu/command_buffer/service/webgpu_interface.h
   class WebGPUInterface {
    public:
     virtual ~WebGPUInterface() = default;
     
     virtual WGPUDevice CreateDevice(...) = 0;
     virtual WGPUBuffer CreateBuffer(...) = 0;
     // ... all WebGPU operations
   };
   
   class DawnBackend : public WebGPUInterface { /*...*/ };
   class WGPUBackend : public WebGPUInterface { /*...*/ };
   ```

3. **Modify WebGPU Decoder**
   ```cpp
   // //gpu/command_buffer/service/webgpu_decoder_impl.cc
   WebGPUDecoderImpl::WebGPUDecoderImpl() {
     backend_ = WebGPUBackendFactory::CreateBackend();
   }
   ```

4. **Runtime Configuration**
   ```cpp
   // Configuration via command-line flag
   --enable-features=ExperimentalWGPURustBackend
   
   // Or runtime detection
   if (base::FeatureList::IsEnabled(features::kWGPURustBackend)) {
     WebGPUBackendFactory::SetBackend(WebGPUBackend::kWGPU);
   }
   ```

**Deliverables:**
- Backend selection working
- Can switch between Dawn and wgpu.rs
- Basic functionality works with both backends

#### Phase 4: Feature Parity (12+ months)

**Objective:** Ensure wgpu.rs backend supports all Dawn features

**Tasks:**

1. **WebGPU Specification Compliance**
   - Pass WebGPU CTS (Conformance Test Suite)
   - All required features implemented
   - Extension support parity

2. **Platform Support**
   - Windows (D3D12) backend working
   - macOS (Metal) backend working
   - Linux (Vulkan) backend working
   - Android support
   - ChromeOS support

3. **Performance Optimization**
   - Minimize FFI overhead
   - Optimize hot paths
   - Add fast paths for common operations
   - Profile and tune

4. **Chrome-Specific Features**
   - Shared image support
   - GPU memory buffer integration
   - Video frame integration
   - Canvas integration

**Deliverables:**
- Full feature parity with Dawn
- Performance within 10% of Dawn
- All tests passing

#### Phase 5: Testing and Validation (6-12 months)

**Objective:** Comprehensive testing and validation

**Tasks:**

1. **Functional Testing**
   - WebGPU CTS full pass rate
   - Chrome WebGPU tests
   - Real-world application testing

2. **Performance Testing**
   - Benchmark suite
   - Real-world workload performance
   - Memory usage analysis

3. **Security Testing**
   - Fuzzing both backends
   - Penetration testing
   - Security audit of FFI layer

4. **Stability Testing**
   - Long-running tests
   - Stress tests
   - Resource leak detection

5. **Gradual Rollout**
   - Finch experiment configuration
   - Canary testing
   - Small percentage rollout
   - Monitor crash rates and performance

**Deliverables:**
- Comprehensive test coverage
- Performance validation
- Security validation
- Rollout plan


### Feature Flag Design

#### Build-Time Flag

```gn
declare_args() {
  enable_wgpu_rust_build = false
}

if (enable_wgpu_rust_build) {
  deps += [ "//third_party/wgpu:wgpu_core" ]
  defines += [ "ENABLE_WGPU_RUST_BACKEND" ]
}
```

#### Runtime Flag

```cpp
namespace features {
BASE_FEATURE(kWGPURustBackend, "WGPURustBackend",
             base::FEATURE_DISABLED_BY_DEFAULT);
}
```

**Command-line activation:**
```bash
chrome --enable-features=WGPURustBackend
chrome --disable-features=WGPURustBackend
```

### Testing Strategy

**1. Duplicate Test Runs**
- Run all WebGPU tests with both backends
- Performance comparison bots
- Fuzzing both implementations

**2. Bot Configuration**
- Dedicated bot fleet for wgpu.rs testing
- Full WebGPU CTS with both backends

**3. Fuzzing**
- FFI-specific fuzzing for adapter layer
- Cross-backend differential fuzzing

### Rollback Strategy

- Finch remote disable capability
- Command-line override
- Automatic fallback on critical errors


---

## Recommendations

### Short-term (0-6 months)

**Primary Recommendation: Continue with Dawn**

**Rationale:**
1. Dawn is working well in production
2. wgpu.rs integration would be massive undertaking  
3. ROI unclear
4. Better to incrementally improve Dawn

**Actionable Items:**

1. **Document Dawn Attack Surface**
   - Comprehensive security audit
   - Document unsafe patterns
   - Prioritize fixes by risk

2. **Improve Dawn Testing**
   - Expand fuzzing coverage
   - More sanitizer runs
   - Better test coverage

3. **Prototype Rust Integration**
   - Small proof-of-concept
   - Measure FFI overhead
   - Assess feasibility

### Medium-term (6-24 months)

**Consider Feature Flag Approach IF:**
- Clear business case emerges
- Team gains Rust expertise
- Performance validated as acceptable

**Go/No-Go Criteria:**
1. Performance within 5% of Dawn
2. Full platform support
3. Zero increase in crash rates
4. No increase in security vulnerabilities
5. Sufficient team Rust expertise

**Hybrid Approach (Recommended):**
1. Use wgpu.rs for high-risk components
2. Keep Dawn for performance-critical paths
3. Gradual migration based on ROI

### Long-term (2+ years)

**Strategic Decision Points:**

1. **Monitor Industry Trends**
   - Firefox wgpu.rs experience
   - WebGPU ecosystem evolution
   - Rust adoption in graphics

2. **Evaluate Chrome Rust Strategy**
   - Align with broader adoption
   - Coordinate with Rust working group

3. **Reassess Periodically**
   - Annual review
   - Update based on new information


---

## Conclusion

**Key Takeaways:**

1. **Dawn vs wgpu.rs is not binary** - each has strengths for different contexts
2. **Memory safety is important** but not the only consideration  
3. **Integration complexity** is a major challenge for wgpu.rs in Chrome
4. **Switchable runtime is feasible** but requires significant investment
5. **Incremental improvements** to Dawn may be more pragmatic

**For Chrome Today:**
- Dawn is better because it's proven, optimized, and team is expert
- Memory safety concerns are real but manageable
- wgpu.rs would require massive investment with uncertain payoff

**For Memory Safety:**
- wgpu.rs would be better if memory safety is absolute top priority
- But achieving this requires solving significant integration challenges
- FFI layer could introduce new security concerns

**For the Future:**
- Switchable approach could be valuable for experimentation
- Allows gradual migration if benefits prove clear
- But doubles maintenance burden

**Recommended Path Forward:**

1. **Continue with Dawn** as primary implementation
2. **Incrementally improve** Dawn safety (tooling, audits, testing)
3. **Prototype wgpu.rs integration** to understand real costs
4. **Make data-driven decisions** based on actual measurements
5. **Consider hybrid approach** using Rust for highest-risk components

The goal should not be "replace Dawn with wgpu.rs" but rather "deliver the most secure, performant WebGPU implementation for Chrome users" - and that may involve components of both.


---

## Appendix A: Key Files and Locations

### Dawn Implementation

```
chromium/
├── gpu/
│   ├── command_buffer/service/
│   │   ├── webgpu_decoder_impl.{cc,h}    # Main WebGPU decoder
│   │   ├── dawn_instance.{cc,h}           # Dawn instance wrapper
│   │   └── dawn_*.{cc,h}                  # Dawn integration files
│   └── webgpu/                            # WebGPU common code
└── third_party/blink/renderer/modules/webgpu/
    ├── gpu.cc                             # Blink WebGPU entry point
    └── dawn_*.{cc,h}                      # Dawn conversions
```

### Proposed wgpu.rs Integration

```
chromium/
├── third_party/wgpu/                      # NEW: wgpu.rs code
│   ├── wgpu-core/                        # Core implementation
│   ├── wgpu-hal/                         # Hardware abstraction
│   ├── chromium_ffi/                     # NEW: FFI layer for Chrome
│   └── BUILD.gn                          # NEW: Build configuration
├── gpu/
│   └── webgpu/
│       ├── wgpu_adapter/                 # NEW: C++ adapter layer
│       │   ├── wgpu_adapter.{cc,h}
│       │   └── wgpu_ffi.h                # FFI declarations
│       └── webgpu_backend.{cc,h}         # NEW: Backend selection
└── base/
    └── features.h                         # Feature flag definition
```

## Appendix B: Further Reading

**Dawn Documentation:**
- Dawn Project: https://dawn.googlesource.com/dawn
- WebGPU Technical Report: ../security/research/graphics/webgpu_technical_report.md

**wgpu.rs Documentation:**
- wgpu.rs GitHub: https://github.com/gfx-rs/wgpu
- wgpu.rs Book: https://wgpu.rs/

**Rust in Chromium:**
- Rust in Chromium: ../rust.md
- Rust FFI Guide: ../rust/ffi.md

**WebGPU Specification:**
- WebGPU Spec: https://www.w3.org/TR/webgpu/
- WebGPU Shading Language: https://www.w3.org/TR/WGSL/

---

**Document Version:** 1.0  
**Last Updated:** 2026-01-29  
**Next Review:** 2026-07-29
