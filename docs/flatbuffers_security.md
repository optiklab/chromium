# FlatBuffers Security: Evaluation of Safety Approaches

This document analyzes the security implications of FlatBuffers usage in
Chromium and evaluates three approaches to improve safety. It also documents
the design decision for the chosen approach.

## Background

[FlatBuffers](https://google.github.io/flatbuffers/) is a cross-platform
serialization library used in Chromium for:

- `components/subresource_filter` – indexed URL-pattern rulesets
- `components/safe_browsing` – client-side phishing models
- `extensions/browser/api/declarative_net_request` – extension rulesets
- Various ML inference components (TensorFlow Lite, MediaPipe)

FlatBuffers performs **zero-copy deserialization**: instead of parsing a
message into objects, it provides direct memory-mapped access into a raw
byte buffer. This is fast, but it means that accessing data from a
*corrupt or malformed* buffer can cause out-of-bounds reads or other
memory-safety violations.

FlatBuffers provides a `Verifier` class that checks structural integrity
(offsets in bounds, no cycles, etc.) before any data is accessed. The
security issue is that **nothing in the C++ type system prevents a developer
from calling `GetRoot<T>()` without first calling `Verify*Buffer()`**.

### Current state in Chromium

All major usage sites in Chromium *do* perform verification before access.
However the safety guarantee is **convention-based, not enforced by the
compiler**. Future changes risk introducing access without verification,
particularly in new code or when refactoring existing code.

---

## Approach 1 – Replace the C++ FlatBuffers library with the Rust `flatbuffers` crate

### Reasoning steps

1. The official FlatBuffers project ships a first-party
   [Rust crate](https://crates.io/crates/flatbuffers) that generates Rust
   code from `.fbs` schema files.
2. The Rust crate's generated accessor methods are safe by default: they
   call the `follow` trait which includes bounds checking on every access,
   not just an upfront verify pass.
3. Replacing the C++ library means rewriting: all schema code-generation
   targets in GN to emit Rust instead of C++, all call sites that read
   FlatBuffers data, and all builder code that writes FlatBuffers data.
4. The `flatc` compiler supports `--rust` flag and is already present in
   `//third_party/flatbuffers:flatc`.

### Pros

- Memory safety enforced by the Rust compiler on every individual field
  access, not just upfront.
- Eliminates the entire class of "forgot to verify" bugs at the language
  level.
- Aligns with Chromium's long-term direction of increasing Rust usage
  for security-critical parsing.

### Cons

- Enormous scope of change: every component using FlatBuffers must be
  migrated (subresource_filter, safe_browsing, declarative_net_request,
  TFLite, etc.).
- Builder code (writing FlatBuffers) has less compelling safety advantages
  and would need separate migration or bridging.
- The `flatbuffers` Rust crate has not yet been vendored into Chromium's
  third-party directory, requiring third-party review and security audit.
- Introduces large diffs that are harder to review and more likely to
  introduce regressions.
- C++ side still needs to interoperate with the generated Rust types via
  CXX, adding FFI complexity.

### Confidence score

**45%** – The approach is theoretically correct but the cost/risk ratio is
unfavorable for an incremental change. A full replacement would be a
multi-month project.

### Verification questions

1. *Does the Rust `flatbuffers` crate actually provide per-field safety, or
   does it still require an upfront `verify()` call?*

   **Answer:** The official Rust crate still recommends calling `root_as_xxx()`
   (which internally calls `flatbuffers::root_unchecked()`) after an explicit
   `flatbuffers::root::<T>(bytes)` which verifies. The `root::<T>()` function
   panics on invalid data. Individual field accessors do *not* independently
   re-verify every access; the upfront verification pass is still the primary
   safety mechanism. The safety improvement over C++ is that the Rust type
   system and borrow checker ensure the buffer lifetime is valid and that
   raw pointers cannot be handed off unsafely.

2. *Would replacing the C++ library break the TFLite integration?*

   **Answer:** TFLite uses its own bundled FlatBuffers (or the system one) and
   does not use Chromium's `//third_party/flatbuffers` directly for model
   parsing. However the `flexbuffers.h` header is explicitly included in
   Chromium's FlatBuffers target for TFLite. A full replacement would need
   to preserve or re-wrap this dependency.

3. *Is the `flatbuffers` Rust crate already present in Chromium's vendor
   directory?*

   **Answer:** No. As of this writing, the crate is not present in
   `//third_party/rust/chromium_crates_io/vendor/`. Adding it requires
   following the process in `//third_party/rust/README-importing-new-crates.md`
   and obtaining third-party and security review.

### Corrected final answer

Approach 1 is the correct *long-term* direction for net-new code that parses
untrusted FlatBuffers, but is **not appropriate as a near-term incremental
change** given its scope, risk, and current codebase maturity.

---

## Approach 2 – Rust wrapper over the existing C++ FlatBuffers library (via CXX)

### Reasoning steps

1. Rather than replacing the entire C++ library, expose the C++
   `flatbuffers::Verifier` and schema-specific `Verify*Buffer()` functions to
   Rust via a thin CXX bridge.
2. Create a safe Rust newtype `VerifiedBuffer<'a>` that can only be
   constructed by calling a Rust `verify()` function (which internally calls
   the C++ verifier).
3. Only `VerifiedBuffer<'a>` grants access to buffer data; callers cannot
   call `GetRoot` equivalents on unverified bytes.
4. New Rust components that need to read FlatBuffer data use
   `VerifiedBuffer<'a>` instead of raw slices.

### Pros

- Enforces verification at the Rust type-system level for new Rust code:
  `VerifiedBuffer<'a>` cannot be constructed without going through the
  verifier.
- Leverages existing CXX infrastructure in Chromium (`//third_party/rust/cxx`
  is already vendored).
- Does not require replacing any existing C++ code.
- The borrow checker ensures the buffer lifetime is respected.
- Gradual migration: new components or rewrites can adopt the Rust API
  while existing C++ code remains untouched.

### Cons

- Still depends on the unsafe C++ library for the actual memory access path.
- Requires schema-specific CXX bridge declarations for each `.fbs` root type
  (because `Verify*Buffer()` is generated per schema).
- Adds FFI overhead and complexity compared to a pure Rust solution.
- C++ callers still use the old unsafe pattern; only Rust callers benefit.

### Confidence score

**65%** – This is a pragmatic incremental approach. The main uncertainty is
the ergonomics of exposing per-schema verifier functions through CXX without
excessive boilerplate.

### Verification questions

1. *Can CXX bridge function pointers like `bool (*)(flatbuffers::Verifier&)` to
   Rust?*

   **Answer:** CXX does not support bridging raw C++ function pointers or
   template types directly. Each schema-specific `Verify*Buffer()` function
   would need a non-template C++ wrapper that CXX can bind to. This is
   manageable but adds boilerplate per schema type.

2. *Does using CXX to call C++ from Rust count as `unsafe` in Chromium's
   security model?*

   **Answer:** CXX FFI requires `allow_unsafe = true` in the GN
   `rust_static_library` target. The Chromium security model (Rule of 2)
   treats CXX boundary calls as requiring sandbox or review for data
   received from untrustworthy sources. The verification step would still
   happen on the C++ side, so the Rust borrow checker protects the
   *lifetime* but not the *content* of the data.

3. *Is there an existing example of calling a C++ function-pointer-taking
   API from Rust via CXX in Chromium?*

   **Answer:** There is no direct example of passing C++ function pointers
   through CXX in the Chromium codebase. Typically CXX bridges named
   functions. Each schema would need a named C++ shim like
   `bool VerifyExtensionRuleset(flatbuffers::Verifier& v)` that CXX can
   bind to directly.

### Corrected final answer

Approach 2 is valuable for **new Rust components** that consume FlatBuffers
data. It provides meaningful safety improvements at the Rust type-system
level, but it requires per-schema FFI boilerplate and does not improve
existing C++ call sites.

---

## Approach 3 – C++ type-safe wrapper enforcing verification before access

### Reasoning steps

1. Create a `VerifiedFlatBuffer<T>` template class with a private
   constructor that can only be called from a `Create()` factory method.
2. `Create()` runs the provided `Verify*Buffer()` function and returns
   `std::optional<VerifiedFlatBuffer<T>>`, returning `std::nullopt` on
   failure.
3. The only way to get a root object pointer is via `root()`, which is
   only accessible from a `VerifiedFlatBuffer<T>` that already passed
   verification.
4. Callers who forget to verify simply cannot call `root()` – it is a
   compile error.
5. This approach uses no new external dependencies (no new Rust crates,
   no new build targets beyond the header itself).

### Pros

- Enforces "verify before access" at the C++ type level.
- Minimal diff: a single header file and a GN target.
- No new dependencies; uses existing `base::span` and `flatbuffers::Verifier`.
- Works for all existing and future `.fbs` schemas without modification.
- Easy to adopt incrementally: replace `flatbuffers::GetRoot<T>(buf)` calls
  with `VerifiedFlatBuffer<T>::Create(span, Verify*Buffer)`.
- Consistent with Chromium's existing C++ style.

### Cons

- C++ still accesses the raw bytes through `flatbuffers::GetRoot<T>` after
  verification; the memory-safety guarantee is still trust-based (the
  verifier implementation could itself have bugs).
- Does not prevent a developer from calling the raw `flatbuffers::GetRoot<T>`
  API from *outside* the wrapper.
- No lifetime tracking equivalent to Rust's borrow checker; the caller
  must ensure the underlying buffer outlives the `VerifiedFlatBuffer<T>`.

### Confidence score

**85%** – This is straightforward to implement correctly, integrates with
the existing codebase immediately, and provides meaningful protection
against "forgotten verification" bugs.

### Verification questions

1. *Does `std::optional<VerifiedFlatBuffer<T>>` work without a copy/move
   constructor issue?*

   **Answer:** Yes. `VerifiedFlatBuffer<T>` stores a `base::span<const uint8_t>`
   which is trivially copyable. The compiler will synthesize appropriate
   copy and move constructors. `std::optional` requires either copy- or
   move-constructibility, which is satisfied.

2. *Can the private constructor be bypassed in C++?*

   **Answer:** In C++ the private constructor can be bypassed through
   `reinterpret_cast` or other undefined behavior, but that would be
   explicitly intentional abuse. It cannot be bypassed accidentally;
   normal usage is forced through `Create()`. This is the same level of
   protection as `base::Token` and similar Chromium types.

3. *Is the verification cost paid twice if a caller already verified
   independently before calling `Create()`?*

   **Answer:** In theory yes – if a caller has already verified the buffer,
   calling `Create()` verifies it a second time. In practice the verifier
   is a linear O(n) scan that is cheap relative to the operations that
   follow. Callers that need to avoid duplicate work can cache the
   `VerifiedFlatBuffer<T>` object.

### Corrected final answer

Approach 3 is the most practical immediate improvement. It costs almost
nothing to implement, eliminates the common mistake of accessing data
without verification, and is immediately applicable to all existing usage
sites.

---

## Recommendation

**Implement Approach 3 now; pursue Approach 2 for new Rust components.**

### Justification

The current codebase already performs verification at all known call sites,
but this is enforced only by convention. A single new call site that skips
verification could be a security bug. The `VerifiedFlatBuffer<T>` wrapper
(Approach 3) eliminates this risk at zero cost in dependencies or
complexity.

Approach 2 (Rust wrapper) is the right direction for **new code** that
reads FlatBuffers data, particularly in sandboxed renderer or utility
processes where additional memory-safety guarantees are most valuable.
It should be pursued when vendoring the `flatbuffers` Rust crate or when
a component is being rewritten in Rust.

Approach 1 (full replacement) is best reserved for net-new components that
have no existing C++ call sites to migrate. It should not be undertaken as
a refactoring exercise for existing components without a compelling
independent reason.

### Summary table

| Approach | Safety mechanism | Implementation cost | Immediate applicability |
|----------|-----------------|---------------------|------------------------|
| 1. Replace with Rust crate | Language-level (Rust borrow checker) | Very High | Low – requires crate vendoring + full migration |
| 2. Rust wrapper via CXX | Type-level (Rust `VerifiedBuffer<'a>` newtype) | Medium | Medium – new Rust code only |
| **3. C++ type-safe wrapper** | **Type-level (`VerifiedFlatBuffer<T>` optional factory)** | **Very Low** | **High – all existing and new C++ code** |

---

## Implementation

`VerifiedFlatBuffer<T>` is implemented in
`//third_party/flatbuffers/safe_flatbuffers.h` and exposed via the
`//third_party/flatbuffers:safe_flatbuffers` GN target.

### Migration example

Before (unsafe – verification is easy to accidentally skip):

```cpp
// In some header/constructor:
const MyNS::MyTable* root_ = nullptr;

// In some method:
flatbuffers::Verifier verifier(data.data(), data.size());
if (!MyNS::VerifyMyTableBuffer(verifier)) {
  return false;
}
root_ = MyNS::GetMyTable(data.data());  // OK if above check is not omitted.
```

After (safe – `root()` is inaccessible without a successful `Create()`):

```cpp
#include "third_party/flatbuffers/safe_flatbuffers.h"

// In some header:
std::optional<VerifiedFlatBuffer<MyNS::MyTable>> verified_table_;

// In some method:
verified_table_ = VerifiedFlatBuffer<MyNS::MyTable>::Create(
    base::as_byte_span(data), MyNS::VerifyMyTableBuffer);
if (!verified_table_) {
  return false;
}
const MyNS::MyTable* root = verified_table_->root();  // Always safe.
```
