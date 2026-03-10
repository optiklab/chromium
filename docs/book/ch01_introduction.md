# Chapter 1: Introduction to Chromium

> *"Chromium is not just a browser. It is a platform for building browsers."*

---

## 1.1 What Is Chromium?

At first glance, Chromium looks like a web browser — it has an address bar, tabs,
and a back button. But the moment you look at its source tree you realize you are
looking at something far larger than any individual application. Chromium is a
**platform**: a collection of reusable engines, libraries, and frameworks that
can be assembled into a browser, a web-app runtime, or the foundation of an
operating system.

The Chromium project lives at
[chromium.googlesource.com/chromium/src](https://chromium.googlesource.com/chromium/src).
At time of writing it contains roughly 35 million lines of code across more than
300,000 files. It is one of the largest active open-source C++ projects in the
world.

### 1.1.1 Chromium vs. Google Chrome

The easiest source of confusion when starting out is the relationship between
**Chromium** and **Google Chrome**. They are not the same thing, though they
share the vast majority of their code.

| Property | Chromium | Google Chrome |
|---|---|---|
| Source code | Fully open-source (BSD license) | Chromium + proprietary additions |
| Logo | Blue monochrome | Colorful Google-branded logo |
| Crash reporting | Disabled | Opt-in telemetry |
| H.264 / AAC codecs | No (patent-encumbered) | Yes |
| Google API keys | Depends on distro | Bundled by Google |
| Sandbox | Depends on distro | Always on |
| Profile location (Linux) | `~/.config/chromium` | `~/.config/google-chrome` |

Think of it this way: **Chromium is the recipe; Google Chrome is the product
Google bakes from that recipe and ships with extra ingredients.**

Other browsers — Microsoft Edge, Opera, Brave, Vivaldi, Samsung Internet — also
build on Chromium. They each apply their own "extra ingredients" on top of the
same open-source base.

### 1.1.2 A Brief Lineage

```
KHTML (KDE, ~2000)
    └── WebKit (Apple, 2003) — used in Safari
            └── Chromium / WebKit (Google, 2008)
                    └── Blink (Google fork, 2013) ← Chromium today
```

When Google first shipped Chrome in 2008 it used Apple's WebKit rendering engine
and a brand-new JavaScript engine called **V8**. In 2013 Google forked WebKit
into its own engine, **Blink**, which now lives in the Chromium source tree under
`//third_party/blink`. V8 also lives in the tree under `//v8`.

---

## 1.2 Why Chromium Matters

### 1.2.1 It Powers Most of the Web

As of 2024 the Chromium engine (in various branded forms) runs in the browsers
used by roughly two-thirds of all internet users. Every time you visit a webpage,
there is a good chance the code interpreting that HTML is ultimately descended
from the Chromium source tree you are looking at right now.

### 1.2.2 It Is an Open-Standards Laboratory

The Chrome team at Google is one of the primary forces driving new web platform
features — Service Workers, WebAssembly, WebGPU, the Streams API, and many
others were prototyped inside Chromium. Understanding how a feature is
implemented in the engine is the clearest way to understand the underlying
standard.

### 1.2.3 It Is a Case Study in Large-Scale C++

Chromium is also simply a masterclass in software engineering. It demonstrates
how to:

- structure a 35-million-line codebase so that any engineer can find what they
  are looking for;
- write safe, portable C++ across Windows, macOS, Linux, Android, and ChromeOS;
- coordinate thousands of contributors through code review, automated testing,
  and continuous integration;
- keep security guarantees as the product evolves at high velocity.

---

## 1.3 The Source Tree: What Lives Where

When you `ls` the root of a Chromium checkout the number of directories is
overwhelming. The good news is that the vast majority of the code you will care
about in this book lives in a small number of top-level directories.

```
chromium/src/
├── base/          # foundational C++ utilities used by everything
├── chrome/        # the Chrome browser application layer
├── components/    # reusable feature modules (autofill, bookmarks, …)
├── content/       # the core multi-process engine abstraction
├── net/           # the networking stack
├── mojo/          # the IPC library
├── services/      # standalone services running via Mojo
├── third_party/
│   └── blink/     # the Blink rendering engine
├── ui/            # UI primitives and the Views widget toolkit
├── v8/            # the V8 JavaScript engine
├── chromeos/      # ChromeOS browser-side code  ← NOT covered here
└── ash/           # ChromeOS window manager / shell  ← NOT covered here
```

The `//` prefix is the Chromium convention for referring to paths relative to the
repository root. You will see it everywhere in design documents and in GN build
files: `//net/base/url_util.cc` means the file
`<repo root>/net/base/url_util.cc`.

### 1.3.1 `//base` — The Foundation

`//base` is the bedrock. It provides:

- **Logging** (`base::LOG`, `DCHECK`, `CHECK`)
- **Strings and encoding** (`base::StringPiece`, UTF-8 utilities)
- **Files and paths** (`base::FilePath`, `base::File`)
- **Time** (`base::Time`, `base::TimeTicks`)
- **Threading and task posting** (`base::TaskRunner`,
  `base::SequencedTaskRunner`, `base::ThreadPool`)
- **Callbacks and bind** (`base::Callback`, `base::BindOnce`)
- **Memory utilities** (`base::WeakPtr`, smart pointer helpers)

Almost every other directory in the tree depends on `//base`. When you are
reading any Chromium source file and you see a `base::` prefix, you know you are
using one of these primitives.

### 1.3.2 `//content` — The Embedding API

`//content` is the most architecturally important directory in the tree. It
contains the **Content API** — a set of C++ interfaces that abstract away the
entire multi-process browser engine. Anything above `//content` (like `//chrome`)
uses the Content API to interact with tabs, frames, web contents, and navigations
without ever needing to know that renderer processes and sandboxes exist.

Think of `//content` as the "kernel" of the browser. It owns:

- The browser process orchestration code
- The renderer process lifecycle
- The navigation pipeline
- The `RenderFrameHost` / `WebContents` abstractions
- Mojo interface dispatch into and out of sandboxed processes

`//chrome` is an *embedder* of `//content`. So is Android WebView. So is the
headless shell used in automated testing. This layering is intentional and
strictly enforced: `//content` must not `#include` any header from `//chrome`.

### 1.3.3 `//chrome` — The Browser Application

`//chrome` is where the browser-as-product lives. It includes:

- The browser window, tabs, and toolbar (built on `//ui/views`)
- The omnibox (address bar) and suggestions
- Settings, extensions, the download shelf
- Application-level prefs and policies
- Platform-specific packaging code

If you are trying to understand how the browser *looks*, start in `//chrome`.
If you are trying to understand how a web page *works*, start in `//content` or
`//third_party/blink`.

### 1.3.4 `//net` — The Networking Stack

`//net` implements everything from raw socket management to HTTP/1.1, HTTP/2,
and QUIC (HTTP/3). It provides:

- `URLRequest` — the main API for making network requests
- The HTTP cache
- Cookie storage and management
- DNS resolution
- TLS/SSL via BoringSSL (`//third_party/boringssl`)

The networking stack runs inside the **browser process** (or a dedicated network
service process on modern Chromium). Chapter 5 covers it in depth.

### 1.3.5 `//mojo` — Inter-Process Communication

Because Chromium runs its components in separate, sandboxed processes, those
components need a safe way to communicate. That mechanism is **Mojo**. Mojo
provides:

- **Message pipes** — bidirectional byte channels between processes
- **Interfaces** — typed, versioned contracts defined in `.mojom` files
- **Bindings** — auto-generated C++ (and JavaScript) code for calling those
  interfaces

You will see `.mojom` files scattered across the tree wherever two processes need
to talk. Chapter 3 covers Mojo in depth.

### 1.3.6 `//third_party/blink` — The Rendering Engine

Blink is the engine that turns HTML, CSS, and JavaScript into a DOM tree, a
style tree, a layout tree, and ultimately painted output. It runs entirely inside
**renderer processes** which are sandboxed away from the rest of the system.
Major sub-areas include:

- `//third_party/blink/renderer/core` — the DOM, CSS engine, HTML parser
- `//third_party/blink/renderer/modules` — Web APIs (WebGL, WebAudio, fetch, …)
- `//third_party/blink/renderer/platform` — low-level graphics and threading

### 1.3.7 `//v8` — The JavaScript Engine

V8 is Google's high-performance JavaScript (and WebAssembly) engine. It is
embedded inside Blink but is also used independently by Node.js and Deno. It
performs:

- Parsing JavaScript source into an AST
- Compiling the AST to bytecode (the Ignition interpreter)
- Profiling hot functions and recompiling them to machine code (Turbofan JIT)
- Garbage collection

Chapter 8 covers V8 in depth.

---

## 1.4 Chromium as a Platform

A common misconception is that Chromium is a monolithic browser. It is not. It
is better understood as a set of **layered platforms**, each of which can be
embedded independently:

```
┌──────────────────────────────────────────────┐
│             //chrome  (browser app)          │
├──────────────────────────────────────────────┤
│           //components  (features)           │
├──────────────────────────────────────────────┤
│           //content  (embedding API)         │  ← the stable boundary
├─────────────────┬────────────────────────────┤
│   //net         │   //third_party/blink      │
│   //mojo        │   //v8                     │
│   //base        │   //ui                     │
└─────────────────┴────────────────────────────┘
```

This layering means that:

- **Android WebView** embeds only `//content` and below, providing a browser
  surface inside Android apps without the Chrome UI.
- **Headless Chrome** embeds `//content` with no visible window, used by
  automated testing tools.
- **Electron** embeds `//content` and layers Node.js on top, allowing desktop
  apps to be written in JavaScript.

Understanding where `//content` sits is the single most important structural
insight in this book. Everything above it is policy; everything below it is
mechanism.

---

## 1.5 The Startup Sequence

When you double-click the Chrome icon, the following sequence unfolds. This is
a simplified view — we will revisit it with more detail in later chapters.

```
  User double-clicks Chrome
         │
         ▼
  Platform entry point
  (wWinMain on Windows / main on Linux/Mac)
  [chrome/app/chrome_exe_main_*]
         │
         ▼
  ChromeMain()
  [chrome/app/chrome_main.cc]
  - Initializes logging, ICU, and other modules
  - Reads --process-type switch
         │
         ├─── (no --process-type) ──────────────────────────▶ BrowserMain()
         │                                                    Creates the browser
         │                                                    window, starts the
         │                                                    main event loop
         │
         ├─── --process-type=renderer ────────────────────▶ RendererMain()
         │                                                    Initializes Blink,
         │                                                    starts handling
         │                                                    navigation commits
         │
         └─── --process-type=gpu / utility / …  ──────────▶ Process-specific main
```

The key insight here is that **Chromium ships as a single executable that knows
how to wear many hats**. When the browser process needs a new renderer it simply
spawns a copy of itself with `--process-type=renderer`. There is no separate
`renderer.exe`. This design makes deployment simple and ensures all processes
share the same binary.

On Linux the sandbox requires a slightly different approach: subprocesses are
created by forking from a helper process rather than re-executing the binary.
This means they do not re-enter `main()`, but they will have already run
the initialization code in `ChromeMain()` from before the fork.

---

## 1.6 Key Concepts to Carry Forward

Before moving to Chapter 2, make sure you have a firm grip on these ideas:

1. **Chromium ≠ Google Chrome.** Chromium is the open-source project; Chrome is
   Google's product built on it with proprietary additions.

2. **`//content` is the embedding API.** It is the stable boundary between the
   browser application and the engine. Code above it does not need to know about
   processes or sandboxes.

3. **The source tree is layered.** `//base` → `//net`, `//mojo`, `//blink`,
   `//v8` → `//content` → `//components` → `//chrome`. Dependencies only flow
   downward.

4. **One executable, many process types.** The same binary is launched with
   different `--process-type` flags to become the browser process, a renderer,
   a GPU process, or a utility process.

5. **ChromeOS is out of scope for this book.** The `//chromeos` and `//ash`
   directories contain OS-specific code for ChromeOS. The browser engine
   concepts covered here apply on all platforms.

---

## Further Reading

- [Chromium Source Tree Overview](../source_tree_overview.md)
- [Chromium vs. Google Chrome (Linux)](../chromium_browser_vs_google_chrome.md)
- [Startup Design Doc](../design/startup.md)
- [Chromium online code search](https://source.chromium.org)

---

*Next: [Chapter 2 – Multi-Process Architecture & Process Model](ch02_multi_process_architecture.md)*
