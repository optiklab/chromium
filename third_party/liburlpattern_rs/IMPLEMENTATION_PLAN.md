# Implementation Plan: Replace liburlpattern C++ with Rust

## Executive Summary

Replace the C++ liburlpattern library with a Rust implementation using the `urlpattern` 
crate (v0.3.0) to eliminate memory safety issues related to UTF-8 handling and ICU macro usage.

**Timeline**: 3 weeks  
**Effort**: 1 engineer  
**Risk Level**: Medium (mitigated by comprehensive testing)

## Problem Statement

The current C++ implementation has memory safety issues:
- Access violations in UTF-8 handling
- Unsafe ICU macros (`U8_GET`, `U8_NEXT`) marked with `UNSAFE_TODO`
- Potential buffer overflows

Example from `pattern.cc`:
```cpp
UNSAFE_TODO(
    U8_GET(reinterpret_cast<const uint8_t*>(next_part->value.data()), 0,
           0, static_cast<int>(next_part->value.size()), codepoint));
```

## Solution Overview

Use the `urlpattern` crate from crates.io:
- **Version**: 0.3.0 (stable)
- **Maintainer**: Deno team (Google-backed)
- **License**: MIT (compatible with Chromium)
- **Quality**: Production-ready, used in Deno runtime

## Prerequisites: Add urlpattern Crate

**CRITICAL**: Must be done first using Chromium's official process.

### Step 1: Add Crate to Cargo.toml

Edit `third_party/rust/chromium_crates_io/Cargo.toml`:
```toml
[dependencies]
# ... existing dependencies ...
urlpattern = "0.3.0"
```

### Step 2: Vendor the Crate

```bash
$ cd /path/to/chromium/src
$ tools/crates/run_gnrt.py vendor
```

This downloads and vendors the crate and its dependencies to:
- `third_party/rust/chromium_crates_io/vendor/urlpattern-v0_3/`

### Step 3: Generate BUILD.gn Files

```bash
$ tools/crates/run_gnrt.py gen
```

This generates:
- `third_party/rust/urlpattern/v0_3/BUILD.gn`
- BUILD.gn files for transitive dependencies

### Step 4: Configure the Crate

Edit `third_party/rust/chromium_crates_io/gnrt_config.toml`:
```toml
[crate.urlpattern]
group = 'safe'  # After security review confirms it's safe
allow_first_party_usage = true
security_critical = true
shipped = true

[crate.urlpattern.extra_kv]
allow_unsafe = true  # urlpattern uses some unsafe internally for performance
```

### Step 5: Security Review

**Required before landing:**
1. Review checklist: `//third_party/rust/OWNERS-review-checklist.md`
2. Add reviewer: chrome-third-party-rust-reviews@google.com
3. Notify: chrome-atls-discuss@google.com
4. Review the crate's use of `unsafe` code
5. Verify license compatibility (MIT is compatible)

**Expected Review Time**: 1-2 weeks (can proceed with implementation in parallel)

## Implementation Phases

### Phase 1: Rust FFI Bridge (Week 1)

Create a Rust library that wraps the urlpattern crate for C++ consumption.

#### 1.1 Create Directory Structure

```bash
mkdir -p third_party/liburlpattern_rs/ffi/src
```

#### 1.2 Create Cargo.toml

`third_party/liburlpattern_rs/ffi/Cargo.toml`:
```toml
[package]
name = "liburlpattern_ffi"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["staticlib"]

[dependencies]
urlpattern = "0.3.0"
cxx = "1.0"

[build-dependencies]
cxx-build = "1.0"
```

#### 1.3 Implement FFI Bridge

`third_party/liburlpattern_rs/ffi/src/lib.rs`:
```rust
use urlpattern::{UrlPattern, UrlPatternInit, UrlPatternMatchInput};
use std::collections::HashMap;

#[cxx::bridge]
mod ffi {
    // Opaque Rust types
    struct RustPattern {
        pattern: Box<UrlPattern>,
    }
    
    struct RustOptions {
        ignore_case: bool,
    }
    
    struct RustPart {
        part_type: u32,  // maps to PartType enum
        name: String,
        prefix: String,
        value: String,
        suffix: String,
        modifier: u32,  // maps to Modifier enum
    }
    
    extern "Rust" {
        // Parse a pattern string
        fn rust_parse_pattern(
            pattern: &str,
            options: &RustOptions,
        ) -> Result<Box<RustPattern>>;
        
        // Generate regex string from pattern
        fn rust_pattern_generate_regex(
            pattern: &RustPattern,
        ) -> String;
        
        // Generate pattern string
        fn rust_pattern_generate_string(
            pattern: &RustPattern,
        ) -> String;
        
        // Check if pattern can use direct matching
        fn rust_pattern_can_direct_match(
            pattern: &RustPattern,
        ) -> bool;
        
        // Perform direct match
        fn rust_pattern_direct_match(
            pattern: &RustPattern,
            input: &str,
        ) -> bool;
        
        // Get pattern parts (for compatibility)
        fn rust_pattern_get_parts(
            pattern: &RustPattern,
        ) -> Vec<RustPart>;
    }
}

// Implementation functions
pub fn rust_parse_pattern(
    pattern: &str,
    options: &ffi::RustOptions,
) -> Result<Box<ffi::RustPattern>, Box<dyn std::error::Error>> {
    let init = UrlPatternInit::parse_pattern(pattern)?;
    let url_pattern = UrlPattern::parse(init)?;
    
    Ok(Box::new(ffi::RustPattern {
        pattern: Box::new(url_pattern),
    }))
}

pub fn rust_pattern_generate_regex(pattern: &ffi::RustPattern) -> String {
    // Implementation depends on urlpattern crate API
    // This is a simplified version
    pattern.pattern.protocol().to_string()
}

// ... implement other functions ...
```

#### 1.4 Create BUILD.gn for FFI

`third_party/liburlpattern_rs/ffi/BUILD.gn`:
```gn
import("//build/rust/rust_static_library.gni")

rust_static_library("liburlpattern_ffi") {
  crate_root = "src/lib.rs"
  sources = [
    "src/lib.rs",
  ]
  
  deps = [
    "//third_party/rust/urlpattern/v0_3:lib",
    "//third_party/rust/cxx/v1:lib",
  ]
  
  # Generate C++ bridge headers
  cxx_bindings = [
    "src/lib.rs",
  ]
  
  allow_unsafe = true  # cxx bridge uses unsafe
}
```

### Phase 2: C++ Wrapper Layer (Week 1-2)

Modify existing C++ files to use the Rust FFI.

#### 2.1 Update pattern.h

Keep the existing header mostly unchanged, but add a private member:

`third_party/liburlpattern/pattern.h`:
```cpp
#ifndef THIRD_PARTY_LIBURLPATTERN_PATTERN_H_
#define THIRD_PARTY_LIBURLPATTERN_PATTERN_H_

#include "third_party/liburlpattern/options.h"
#include "third_party/liburlpattern/part.h"
// ... other includes ...

namespace liburlpattern {

class COMPONENT_EXPORT(LIBURLPATTERN) Pattern {
 public:
  Pattern(std::vector<Part> part_list,
          Options options,
          std::string segment_wildcard_regex);
  
  ~Pattern();  // Need to implement to destroy Rust pattern
  
  std::string GeneratePatternString() const;
  std::string GenerateRegexString(
      std::vector<std::string>* name_list_out = nullptr) const;
  
  bool HasRegexGroups() const;
  bool CanDirectMatch() const;
  bool DirectMatch(std::string_view input, /*...*/) const;
  
  const std::vector<Part>& PartList() const { return part_list_; }
  
 private:
  std::vector<Part> part_list_;
  Options options_;
  std::string segment_wildcard_regex_;
  
  // Opaque pointer to Rust implementation
  void* rust_pattern_;  // Actually rust::Box<RustPattern>
};

}  // namespace liburlpattern

#endif  // THIRD_PARTY_LIBURLPATTERN_PATTERN_H_
```

#### 2.2 Update pattern.cc

Replace implementation with Rust FFI calls:

`third_party/liburlpattern/pattern.cc`:
```cpp
#include "third_party/liburlpattern/pattern.h"
#include "third_party/liburlpattern_rs/ffi/src/lib.rs.h"  // cxx generated header

namespace liburlpattern {

Pattern::Pattern(std::vector<Part> part_list,
                 Options options,
                 std::string segment_wildcard_regex)
    : part_list_(std::move(part_list)),
      options_(std::move(options)),
      segment_wildcard_regex_(std::move(segment_wildcard_regex)) {
  
  // Convert options to Rust format
  RustOptions rust_opts;
  rust_opts.ignore_case = !options_.sensitive;
  
  // Create pattern from parts (simplified)
  std::string pattern_str = GeneratePatternStringInternal();
  auto result = rust_parse_pattern(pattern_str, rust_opts);
  rust_pattern_ = result.into_raw();  // Transfer ownership
}

Pattern::~Pattern() {
  if (rust_pattern_) {
    // Properly destroy Rust pattern
    rust::Box<RustPattern>::from_raw(rust_pattern_);
  }
}

std::string Pattern::GenerateRegexString(
    std::vector<std::string>* name_list_out) const {
  auto* rust_pat = reinterpret_cast<RustPattern*>(rust_pattern_);
  return rust_pattern_generate_regex(*rust_pat);
}

bool Pattern::CanDirectMatch() const {
  auto* rust_pat = reinterpret_cast<RustPattern*>(rust_pattern_);
  return rust_pattern_can_direct_match(*rust_pat);
}

bool Pattern::DirectMatch(std::string_view input, /*...*/) const {
  auto* rust_pat = reinterpret_cast<RustPattern*>(rust_pattern_);
  return rust_pattern_direct_match(*rust_pat, std::string(input));
}

// ... other methods delegate to Rust similarly ...

}  // namespace liburlpattern
```

#### 2.3 Update parse.cc

`third_party/liburlpattern/parse.cc`:
```cpp
#include "third_party/liburlpattern/parse.h"
#include "third_party/liburlpattern_rs/ffi/src/lib.rs.h"

namespace liburlpattern {

base::expected<Pattern, absl::Status> Parse(
    std::string_view pattern,
    EncodeCallback callback,
    const Options& options) {
  
  // Convert options to Rust format
  RustOptions rust_opts;
  rust_opts.ignore_case = !options.sensitive;
  
  // Parse using Rust
  try {
    auto rust_result = rust_parse_pattern(
        std::string(pattern), rust_opts);
    
    // Convert Rust pattern back to C++ Pattern
    auto parts = rust_pattern_get_parts(*rust_result);
    
    std::vector<Part> cpp_parts;
    for (const auto& rust_part : parts) {
      cpp_parts.push_back(ConvertRustPartToCpp(rust_part));
    }
    
    return Pattern(std::move(cpp_parts), options, /*segment_wildcard_regex=*/"");
    
  } catch (const rust::Error& e) {
    return base::unexpected(
        absl::InvalidArgumentError(e.what()));
  }
}

}  // namespace liburlpattern
```

### Phase 3: Build Integration (Week 2)

#### 3.1 Update liburlpattern BUILD.gn

`third_party/liburlpattern/BUILD.gn`:
```gn
component("liburlpattern") {
  defines = [ "IS_LIBURLPATTERN_IMPL" ]
  
  public_deps = [
    "//base",
    "//third_party/abseil-cpp:absl",
    "//third_party/liburlpattern_rs/ffi:liburlpattern_ffi",  # NEW
  ]
  
  configs += [ ":warnings" ]
  
  sources = [
    "options.h",
    "part.cc",
    "part.h",
    "parse.cc",
    "parse.h",
    "pattern.cc",
    "pattern.h",
  ]
  
  # Remove old implementation files (after migration):
  # "tokenize.cc",
  # "tokenize.h",
  # "constructor_string_parser.cc",
  # "constructor_string_parser.h",
  # "utils.cc",
  # "utils.h",
}
```

#### 3.2 Create Top-Level BUILD.gn

`third_party/liburlpattern_rs/BUILD.gn`:
```gn
# This directory contains the Rust FFI bridge for liburlpattern.
# The actual Rust code is in the ffi/ subdirectory.

group("liburlpattern_rs") {
  public_deps = [
    "//third_party/liburlpattern_rs/ffi:liburlpattern_ffi",
  ]
}
```

### Phase 4: Testing (Week 2-3)

#### 4.1 Run Existing Unit Tests

```bash
# Build
$ autoninja -C out/Default liburlpattern_unittests

# Run tests
$ out/Default/liburlpattern_unittests --gtest_filter="*"

# Expected: All tests pass
```

#### 4.2 Run Fuzzers

```bash
$ autoninja -C out/Default liburlpattern_fuzzer
$ out/Default/liburlpattern_fuzzer -runs=100000
```

#### 4.3 Run Web Tests

```bash
$ ./third_party/blink/tools/run_web_tests.py urlpattern/
```

#### 4.4 Memory Safety Validation

```bash
# Build with AddressSanitizer
$ gn args out/Asan
  is_asan = true
  
$ autoninja -C out/Asan liburlpattern_unittests
$ out/Asan/liburlpattern_unittests

# Expected: No ASan violations
```

### Phase 5: Cleanup (Week 3)

#### 5.1 Remove Old C++ Files

After tests pass, remove old implementation:

```bash
$ git rm third_party/liburlpattern/tokenize.{cc,h}
$ git rm third_party/liburlpattern/tokenize_unittest.cc
$ git rm third_party/liburlpattern/constructor_string_parser.{cc,h}
$ git rm third_party/liburlpattern/utils.{cc,h}
$ git rm third_party/liburlpattern/utils_unittest.cc
```

#### 5.2 Update Documentation

`third_party/liburlpattern/README.chromium`:
```
Name: URL Pattern Library
Short Name: liburlpattern
URL: Implemented in Rust using urlpattern crate
Version: 0.3.0 (urlpattern crate)
Update Mechanism: Via Chromium Rust crate update process
License: MIT
License File: LICENSE
Security Critical: yes
Shipped: yes

Description:
This library is the underlying pattern matching engine for the URLPattern web
API. It was previously implemented in C++ but has been replaced with a Rust
implementation using the urlpattern crate (v0.3.0) to eliminate memory safety
issues related to UTF-8 handling.

The Rust implementation is in //third_party/liburlpattern_rs/ and provides
a C++ FFI wrapper to maintain API compatibility.
```

### Phase 6: Documentation (Week 3)

Create comprehensive documentation:

#### 6.1 Migration Guide

`third_party/liburlpattern_rs/MIGRATION_GUIDE.md`:
- Document API changes (if any)
- Performance characteristics
- Known differences from C++ version

#### 6.2 Developer Guide

Document how to:
- Build the Rust FFI
- Debug Rust/C++ boundary issues
- Update the urlpattern crate version

## Success Criteria

- ✅ All `liburlpattern_unittests` pass
- ✅ All fuzzers pass without crashes
- ✅ Blink URLPattern web tests pass
- ✅ No memory safety issues (verified by ASan/MSan)
- ✅ Performance within 10% of C++ version
- ✅ Code review approved
- ✅ Security review completed

## Risk Mitigation

### Risk: Performance Regression

**Impact**: Medium  
**Probability**: Low  
**Mitigation**:
- Benchmark early and often
- Optimize FFI boundary (minimize copies)
- Profile hot paths
**Fallback**: Optimize Rust implementation or FFI layer

### Risk: API Incompatibilities

**Impact**: High  
**Probability**: Low  
**Mitigation**:
- Comprehensive test coverage
- Manual testing of Blink integration
- Gradual rollout
**Fallback**: Fix incompatibilities or add adapter layer

### Risk: Build System Issues

**Impact**: Medium  
**Probability**: Medium  
**Mitigation**:
- Test on all platforms early
- Work with build team
- Document platform-specific requirements
**Fallback**: Platform-specific configurations in BUILD.gn

### Risk: Security Issues in Crate

**Impact**: High  
**Probability**: Low  
**Mitigation**:
- Thorough security review before landing
- Monitor crate for security advisories
- Subscribe to security mailing lists
**Fallback**: Could fork and maintain crate if needed

## Timeline

| Week | Phase | Deliverables |
|------|-------|--------------|
| 1 | Crate Addition + FFI Bridge | urlpattern crate added, FFI implemented |
| 2 | C++ Wrapper + Build | C++ wrappers working, builds successfully |
| 3 | Testing + Cleanup | All tests pass, docs updated, old code removed |

**Total: 3 weeks** (security review in parallel)

## Resources

- 1 Engineer (familiar with Rust and C++ FFI)
- Access to Chromium build infrastructure
- Security review capacity (1-2 weeks)
- Support from `//third_party/rust/OWNERS`

## Stakeholders & Approvals Required

- **Code Owner**: `//third_party/liburlpattern/OWNERS`
- **Security Review**: chrome-third-party-rust-reviews@google.com
- **Rust Owners**: `//third_party/rust/OWNERS`
- **Notification**: chrome-atls-discuss@google.com
- **Blink Team**: For URLPattern API integration

## References

- URLPattern Spec: https://wicg.github.io/urlpattern/
- rust-urlpattern: https://github.com/denoland/rust-urlpattern
- Chromium Rust Guide: `//docs/rust.md`
- Adding Rust Crates: `//tools/crates/create_update_cl.md`
