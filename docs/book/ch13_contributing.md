# Chapter 13: Contributing to Chromium

## Introduction

Chromium is one of the largest open-source projects in the world, with thousands
of contributors and millions of lines of code. This chapter gives you the
practical knowledge to make your first contribution—from setting up the build
system to getting your code reviewed and landed.

---

## 13.1 The Build System: GN + Ninja

Chromium does **not** use CMake or autoconf. It uses a two-stage system:

1. **GN** (*Generate Ninja*) reads `BUILD.gn` files and produces `build.ninja`
   files.
2. **Ninja** reads the generated `build.ninja` files and drives the actual
   compilation.

```
Source tree (*.cc, *.h, BUILD.gn files)
        ↓  gn gen out/Default
Ninja build files (out/Default/*.ninja)
        ↓  ninja -C out/Default chrome
Compiled binaries (chrome, unit_tests, …)
```

### Key GN concepts

| Concept | Meaning |
|---------|---------|
| `source_set` | A group of `.cc`/`.h` files compiled into a `.a` archive |
| `static_library` | Like `source_set` but always generates a `.a` |
| `shared_library` | A `.so`/`.dll` |
| `executable` | A final binary |
| `mojom` | Generates Mojo C++ bindings from a `.mojom` IDL file |
| `//path/to:target` | Absolute GN label |
| `//` | Root of the source tree |

A minimal `BUILD.gn` example:

```python
source_set("my_feature") {
  sources = [
    "my_feature.cc",
    "my_feature.h",
  ]
  deps = [
    "//base",
    "//content/public/browser",
  ]
}
```

### Generating and building

```bash
# Generate build files (only needed once, or after BUILD.gn changes)
gn gen out/Default

# Incremental build of the browser
autoninja -C out/Default chrome

# Build and run unit tests for the net module
autoninja -C out/Default net_unittests
./out/Default/net_unittests
```

`autoninja` is a wrapper that automatically sets the right `-j` parallelism.

---

## 13.2 Code Style and Reviews

### Coding style

Chromium follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with Chromium-specific additions documented in
[`//styleguide/c++/c++.md`](../../styleguide/c++/c++.md).

Key points:
- **Naming**: `CamelCase` for types/classes, `snake_case` for variables and
  functions, `kCamelCase` for constants, `MACRO_CASE` for macros.
- **Header guards**: Use `#pragma once` (Chromium uses `#ifndef` guards in most
  existing files but `#pragma once` for new files is acceptable).
- **Ownership**: Prefer `std::unique_ptr` for single owners; avoid raw owning
  pointers.
- **No raw `new`/`delete`**: Use smart pointers or factory functions.
- **Callbacks**: Use `base::OnceCallback` / `base::RepeatingCallback` instead of
  raw function pointers or `std::function`.
- **No exceptions**: Chromium is compiled with exceptions disabled.

### The review process

Chromium uses **Gerrit** for code review at
[chromium-review.googlesource.com](https://chromium-review.googlesource.com).

```
Write code → git commit → git cl upload
        ↓
Gerrit change (CL = Change List)
        ↓
Reviewers comment / LGTM (Looks Good To Me)
        ↓
An OWNER approves
        ↓
CQ (Commit Queue) runs bots
        ↓
Auto-merged to main
```

#### OWNERS files

Every directory has an `OWNERS` file listing who may approve changes in that
directory. You must get approval from at least one owner of every directory your
CL touches.

```
# src/net/OWNERS
mmenke@chromium.org
eroman@chromium.org
# ... etc.
```

Use `git cl owners` or the Gerrit UI to find the right reviewers.

---

## 13.3 Writing Tests

Testing is mandatory for new code. Chromium has several test frameworks:

### Unit tests (`_unittest.cc`)

Test a single class or function in isolation. Use **GTest** and **GMock**:

```cpp
#include "testing/gtest/include/gtest/gtest.h"

TEST(MyFeatureTest, ReturnsTrueForValidInput) {
  MyFeature feature;
  EXPECT_TRUE(feature.Process("valid"));
}
```

Build and run:
```bash
autoninja -C out/Default components_unittests
./out/Default/components_unittests --gtest_filter="MyFeature*"
```

### Browser tests (`_browsertest.cc`)

Tests that need a real browser process. They are slower but test real
integration. Inherit from `InProcessBrowserTest`:

```cpp
class MyBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override { /* ... */ }
};

IN_PROC_BROWSER_TEST_F(MyBrowserTest, LoadsPage) {
  ASSERT_TRUE(NavigateToURL(shell(), GURL("https://example.com")));
}
```

### Web tests (layout tests)

Tests for web platform behaviour live in
`//third_party/blink/web_tests/`. They load an HTML file and compare output
(DOM, pixel snapshot, or console text) against a baseline. Run with:

```bash
python3 third_party/blink/tools/run_web_tests.py fast/dom/
```

### Fuzzing

Chromium uses **libFuzzer** and **ClusterFuzz** to automatically find
crashes and security bugs. Add a fuzz target by creating a
`_fuzzer.cc` file:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  MyParser::Parse(base::span<const uint8_t>(data, size));
  return 0;
}
```

---

## 13.4 Finding Your Way Around the Codebase

### Key directories for common tasks

| Task | Directory |
|------|-----------|
| Fix a networking bug | `//net/` |
| Change browser UI | `//chrome/browser/ui/` |
| Modify HTML/CSS rendering | `//third_party/blink/renderer/core/` |
| Improve V8 / JS | `//v8/` |
| Fix an IPC interface | Find the `.mojom` file in the relevant component |
| Change the compositor | `//cc/` |
| Modify GPU handling | `//gpu/` |
| Security policy | `//content/browser/child_process_security_policy*` |

### Useful tools

- **`git cl`**: The `depot_tools` wrapper around git for uploading CLs.
- **`gn refs out/Default //net:net`**: Find what targets depend on `//net`.
- **`gn desc out/Default //chrome/browser:browser`**: Describe a target
  (sources, deps, etc.).
- **`chrome://tracing`**: Record and view performance traces.
- **`chrome://net-internals`**: Live view of the networking stack.
- **`chrome://gpu`**: GPU feature status and diagnostics.
- **Source code search**: [source.chromium.org](https://source.chromium.org)

---

## 13.5 Chromium OS Note

Many directories you'll encounter have subdirectories named `chromeos/` or
`ash/`. These contain code specific to **ChromeOS** (the operating system),
which ships a Chromium-based browser as its core UI shell (`//ash`). This book
focuses exclusively on the browser engine. ChromeOS-specific UI, system
services, and hardware integration are outside its scope—if you are
contributing to a browser-level feature, you can generally ignore `//ash` and
`//chromeos`.

---

## 13.6 The Commit Queue and Continuous Integration

When your CL has the required approvals you mark it as **CQ+2** (Commit Queue).
The CQ:
1. Runs a large suite of automated tests on many platforms (Linux, Mac, Windows,
   Android, ChromeOS).
2. If all bots are green the CL is automatically merged.
3. If a bot fails you receive a message and must fix the issue before re-trying.

CI bots are managed through **LUCI** (Layered Universal Continuous Integration).
You can inspect bot results at [ci.chromium.org](https://ci.chromium.org).

---

## 13.7 Summary

| Step | Action |
|------|--------|
| 1 | Fetch depot_tools; `gclient sync` |
| 2 | `gn gen out/Default` |
| 3 | `autoninja -C out/Default chrome` |
| 4 | Write code following the style guide |
| 5 | Write unit tests (and browser tests if needed) |
| 6 | `git cl upload` → add reviewers |
| 7 | Address review comments → LGTM |
| 8 | CQ+2 → auto-merged |

With the knowledge from the previous twelve chapters you now understand *why*
the code is structured the way it is—the process model, the Mojo interfaces,
the rendering pipeline, the security layers. That context makes it far easier
to contribute to any part of the codebase with confidence.

---

*End of book. See the [README](README.md) for the full table of contents.*
