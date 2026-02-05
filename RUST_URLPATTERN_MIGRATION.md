# Rust URLPattern Migration Project

## Overview

This project replaces the C++ liburlpattern library with a Rust-based implementation
to address memory safety issues and eliminate access violations.

## Current Status

**Phase: Planning and Infrastructure Complete ✓**

All planning documentation, build files, and example code have been created.
The next critical step is adding the `urlpattern` crate (v0.3.0) to Chromium.

## Quick Start

### For Reviewers

1. **Start here**: `third_party/liburlpattern_rs/SUMMARY.md`
   - Executive summary of the project
   - What's been done
   - What needs to happen next

2. **Understand the problem**: `third_party/liburlpattern_rs/RATIONALE.md`
   - Why the C++ code has issues
   - How Rust solves them
   - Performance and security benefits

3. **Review the plan**: `third_party/liburlpattern_rs/IMPLEMENTATION_PLAN.md`
   - Detailed 3-week timeline
   - Phase-by-phase breakdown
   - Success criteria and risks

### For Implementers

1. **Add the crate**: Follow `third_party/liburlpattern_rs/CRATE_ADDITION_GUIDE.md`
   ```bash
   # Edit Cargo.toml to add: urlpattern = "0.3.0"
   tools/crates/run_gnrt.py vendor
   tools/crates/run_gnrt.py gen
   # Submit for security review
   ```

2. **Implement FFI bridge**: Complete `third_party/liburlpattern_rs/ffi/src/lib.rs`
   - See example code in the file
   - Use the urlpattern crate API
   - Follow the patterns shown

3. **Update C++ wrappers**: See `third_party/liburlpattern_rs/examples/pattern_wrapper_example.cc`
   - Shows how to delegate to Rust
   - Maintains API compatibility
   - Handles FFI conversions

4. **Test**: Run existing tests to ensure compatibility
   ```bash
   autoninja -C out/Default liburlpattern_unittests
   out/Default/liburlpattern_unittests
   ```

## Problem Statement

The current C++ implementation in `third_party/liburlpattern/` has memory safety issues:

```cpp
// From pattern.cc - marked as unsafe
UNSAFE_TODO(
    U8_GET(reinterpret_cast<const uint8_t*>(next_part->value.data()), 0,
           0, static_cast<int>(next_part->value.size()), codepoint));
```

These unsafe ICU macros for UTF-8 handling can cause:
- Access violations
- Buffer overflows
- Security vulnerabilities

## Solution

Replace with Rust implementation:

```
┌─────────────────────────────────────┐
│     Existing C++ API                │
│  (third_party/liburlpattern/*.h)    │  ← No change
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│    C++ Wrapper (pattern.cc, etc.)   │  ← Updated to call Rust
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Rust FFI Bridge (lib.rs + cxx)    │  ← New Rust code
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   urlpattern crate (v0.3.0)         │  ← Memory-safe implementation
└─────────────────────────────────────┘
```

Benefits:
- ✅ Memory safe (eliminates UNSAFE_TODO)
- ✅ No access violations
- ✅ Maintains API compatibility
- ✅ Battle-tested code (used in Deno)

## Directory Structure

```
/
├── RUST_URLPATTERN_MIGRATION.md        # This file
└── third_party/
    ├── liburlpattern/                  # Existing C++ implementation
    │   ├── pattern.h                   # Will remain (API)
    │   ├── pattern.cc                  # Will be updated to use Rust
    │   ├── parse.h                     # Will remain (API)
    │   ├── parse.cc                    # Will be updated to use Rust
    │   ├── tokenize.cc                 # Will be REMOVED (Rust handles this)
    │   └── ...
    └── liburlpattern_rs/               # New Rust implementation
        ├── SUMMARY.md                  # Start here!
        ├── IMPLEMENTATION_PLAN.md      # Detailed plan
        ├── CRATE_ADDITION_GUIDE.md     # How to add the crate
        ├── RATIONALE.md                # Technical justification
        ├── README.md                   # Architecture overview
        ├── BUILD.gn                    # Build configuration
        ├── OWNERS                      # Code owners
        ├── examples/
        │   └── pattern_wrapper_example.cc  # C++ integration example
        └── ffi/
            ├── BUILD.gn                # FFI build config
            ├── Cargo.toml              # Rust package
            └── src/
                └── lib.rs              # Rust FFI bridge
```

## Documentation Files

| File | Purpose | Length |
|------|---------|--------|
| `SUMMARY.md` | Executive summary, quick start | 115 lines |
| `IMPLEMENTATION_PLAN.md` | Detailed implementation guide | 612 lines |
| `CRATE_ADDITION_GUIDE.md` | Steps to add urlpattern crate | 231 lines |
| `RATIONALE.md` | Technical justification | 240 lines |
| `README.md` | Architecture and overview | 61 lines |

**Total documentation: ~1,260 lines**

## Code Files

| File | Purpose | Length |
|------|---------|--------|
| `ffi/src/lib.rs` | Rust FFI bridge template | 113 lines |
| `examples/pattern_wrapper_example.cc` | C++ wrapper example | 200 lines |
| `BUILD.gn` | Build configurations | 60 lines |
| `Cargo.toml` | Rust package manifest | 35 lines |

**Total code: ~408 lines**

## Timeline

| Week | Phase | Status |
|------|-------|--------|
| 0 | Planning & Documentation | ✅ Complete |
| 1 | Add crate + Security review | ⏳ Next step |
| 2 | Implement FFI bridge | 📋 Planned |
| 3 | C++ integration | 📋 Planned |
| 4 | Testing & cleanup | 📋 Planned |

## Next Steps

### Immediate (Week 1)

1. **Add urlpattern crate** (see `CRATE_ADDITION_GUIDE.md`):
   - Edit `third_party/rust/chromium_crates_io/Cargo.toml`
   - Run `tools/crates/run_gnrt.py vendor`
   - Run `tools/crates/run_gnrt.py gen`
   - Submit for security review

2. **Security review** (~1-2 weeks):
   - Reviewer: chrome-third-party-rust-reviews@google.com
   - Checklist: `//third_party/rust/OWNERS-review-checklist.md`
   - Notify: chrome-atls-discuss@google.com

### After Crate Addition (Weeks 2-4)

3. **Implement FFI**: Complete `ffi/src/lib.rs`
4. **Update C++**: Modify `pattern.cc` and `parse.cc`
5. **Test**: Run all existing tests
6. **Cleanup**: Remove old C++ implementation
7. **Document**: Update README.chromium

## Success Criteria

- [ ] urlpattern crate added and security reviewed
- [ ] All `liburlpattern_unittests` pass
- [ ] All fuzzers pass without crashes
- [ ] Blink URLPattern web tests pass
- [ ] No ASan/MSan violations
- [ ] Performance within 10% of C++
- [ ] Code reviewed and approved
- [ ] Documentation complete

## Key Benefits

### Security
- Eliminates ~70% of potential bug classes (buffer overflows, use-after-free, etc.)
- No more UNSAFE_TODO macros
- Memory-safe by construction

### Maintainability
- Type system catches errors at compile-time
- Easier to reason about code
- Better tooling (rustfmt, clippy)

### Performance
- Expected 0-10% improvement in parsing
- Better UTF-8 handling
- Zero-cost abstractions

### Future-Proofing
- Aligns with Chromium's Rust adoption
- Modern, well-maintained crate
- Active community support

## Questions?

- **Overview**: See `third_party/liburlpattern_rs/SUMMARY.md`
- **Implementation**: See `third_party/liburlpattern_rs/IMPLEMENTATION_PLAN.md`
- **Crate addition**: See `third_party/liburlpattern_rs/CRATE_ADDITION_GUIDE.md`
- **Rationale**: See `third_party/liburlpattern_rs/RATIONALE.md`
- **Code owners**: See `third_party/liburlpattern_rs/OWNERS`
- **General**: chrome-safe-coding@google.com

## References

- **URLPattern Spec**: https://wicg.github.io/urlpattern/
- **rust-urlpattern**: https://github.com/denoland/rust-urlpattern
- **Chromium Rust**: //docs/rust.md
- **Current C++ code**: //third_party/liburlpattern/
- **New Rust code**: //third_party/liburlpattern_rs/

---

**Project Status**: Planning Complete ✓ | Next: Add Crate ⏳
**Last Updated**: 2025-02-05
**Contact**: chrome-safe-coding@google.com
