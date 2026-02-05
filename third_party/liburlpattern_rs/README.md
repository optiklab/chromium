# Rust URLPattern Implementation

This directory contains documentation and build files for replacing the C++ liburlpattern 
library with a Rust-based implementation using the `rust-urlpattern` crate (v0.3.0).

## Why Rust?

The previous C++ implementation had several memory safety issues:
- Access violations related to UTF-8 handling
- Unsafe use of ICU macros (`U8_GET`, `U8_NEXT`) marked with `UNSAFE_TODO`
- Potential buffer overflows in string parsing

The Rust implementation eliminates these issues through Rust's memory safety guarantees.

## Implementation Approach

See `IMPLEMENTATION_PLAN.md` for the complete, detailed plan.

## Directory Structure (After Implementation)

```
third_party/liburlpattern_rs/
├── README.md              # This file
├── IMPLEMENTATION_PLAN.md # Detailed implementation guide
├── BUILD.gn               # GN build rules (to be created)
├── DEPS                   # Dependencies (to be created)
├── OWNERS                 # Code owners (to be created)
├── LICENSE                # MIT license
└── ffi/                   # Rust FFI bridge (to be created)
    ├── src/
    │   ├── lib.rs        # Main Rust FFI implementation
    │   └── bridge.rs     # cxx bridge definitions
    └── BUILD.gn
```

## Key Files

- **IMPLEMENTATION_PLAN.md**: Complete step-by-step implementation guide
- **BUILD.gn**: Build configuration (once implemented)
- **ffi/**: Rust FFI bridge code (once implemented)

## Prerequisites

Before implementing, the `urlpattern` crate must be added to Chromium:
1. Add to `third_party/rust/chromium_crates_io/Cargo.toml`
2. Run `tools/crates/run_gnrt.py vendor`
3. Run `tools/crates/run_gnrt.py gen`
4. Complete security review

See IMPLEMENTATION_PLAN.md for detailed steps.

## Status

**Current Phase**: Planning and Documentation
**Next Steps**: Add urlpattern crate to Chromium (requires security review)

## Contact

For questions about this implementation:
- See `//third_party/liburlpattern/OWNERS`
- Contact chrome-safe-coding@google.com for Rust/security questions
