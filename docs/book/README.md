# Inside Chromium: A Developer's Guide to the Browser Engine

## Preface

### What This Book Is

This book is a guided tour through the Chromium browser engine — one of the
largest and most sophisticated C++ codebases in existence. Rather than a
reference manual or a collection of design documents, it is written to be read
from front to back, like a textbook. Each chapter builds on the last, and by the
end you should have a clear mental model of how a modern browser works from
startup to rendered pixel.

The source material for this book comes directly from the Chromium source tree,
its official design documents, and the project's public documentation. Where
helpful, code examples are drawn from real Chromium files so that you can follow
along in your own checkout.

### A Note on Scope

The Chromium project is vast. It is not only a web browser — it is also the
foundation for ChromeOS, Android WebView, the Electron framework, and countless
other products. **This book focuses on the browser engine and browser
application.** ChromeOS is a separate project; when Chromium is used as part of
ChromeOS the OS-specific parts live in separate directories (e.g., `//chromeos`,
`//ash`) and are not covered here.

### Who This Book Is For

This book is written for first-year university students who are comfortable
reading C++ and want to understand how a real-world, production-quality system is
designed and built. You do not need to know anything about browsers or compiler
theory before starting. A working knowledge of pointers, classes, and basic data
structures is sufficient.

If you have already built toy projects and are wondering how professional
software at internet scale is structured, this book is for you.

### How to Use This Book

Read the chapters in order. Each chapter ends with a short list of **Key
Concepts** to review before moving on. Where relevant, you are pointed to actual
source files in the tree — use `grep`, `git log`, or the online code search at
[source.chromium.org](https://source.chromium.org) to explore further.

You do not need to build Chromium to follow along, but if you want to, the
official instructions are at
[chromium.googlesource.com/chromium/src/+/main/docs/get_the_code.md](https://chromium.googlesource.com/chromium/src/+/main/docs/get_the_code.md).

### A Brief History of Chromium

In the early 2000s the browser landscape was dominated by Internet Explorer.
Mozilla Firefox challenged that dominance by being fast and open, but by 2006
Google had grown so dependent on the web that it decided the web platform itself
needed a faster, more secure vehicle. The result, released in September 2008, was
**Google Chrome**.

Chrome was not built from scratch. Google forked **WebKit** — itself an Apple
fork of the KDE project's KHTML engine — to use as its rendering engine, and
paired it with a brand-new JavaScript engine called **V8** that it wrote
internally. Chrome was also the first mainstream browser to adopt a
**multi-process architecture**, where each tab runs in its own sandboxed process.
This made individual tab crashes contained and introduced a new layer of security
that is now considered industry standard.

The underlying source code was released simultaneously as the **Chromium**
open-source project. Google Chrome is Chromium built, packaged, and distributed
by Google with a set of proprietary additions. The two names are often used
interchangeably in casual conversation, but the distinction matters and is
explained fully in Chapter 1.

In 2013, Google forked its own copy of WebKit into a new engine called
**Blink**, which remains the rendering engine inside Chromium to this day. V8
continues to power JavaScript execution and is also used standalone by Node.js
and Deno.

Today Chromium forms the basis of Google Chrome, Microsoft Edge, Opera, Brave,
Vivaldi, Samsung Internet, and dozens of other browsers. Understanding Chromium
is, in a very real sense, understanding the modern web.

---

## Table of Contents

| Chapter | Title |
|---------|-------|
| **1** | [Introduction to Chromium](ch01_introduction.md) |
| **2** | [Multi-Process Architecture & Process Model](ch02_architecture.md) |
| **3** | [Mojo IPC – How Processes Talk to Each Other](ch03_mojo_ipc.md) |
| **4** | Life of a URL – The Browser Pipeline |
| **5** | The Networking Stack |
| **6** | The Renderer Process & Blink |
| **7** | HTML, CSS Parsing and the Rendering Pipeline |
| **8** | The JavaScript Engine (V8) |
| **9** | Security Architecture |
| **10** | GPU, Graphics & Compositing |
| **11** | Memory Management |
| **12** | Browser UI, Navigation & Sessions |
| **13** | Contributing to Chromium |

---

### Chapter Summaries

**Chapter 1 – Introduction to Chromium**
What Chromium is, how it relates to Google Chrome, and how to navigate the
source tree. The content layer, key directories, and the browser startup
sequence.

**Chapter 2 – Multi-Process Architecture & Process Model**
Why Chromium runs each tab in a separate process. The browser process, renderer
processes, utility processes, and the GPU process. How crashes are isolated and
what the sandboxing model buys you.

**Chapter 3 – Mojo IPC**
The problem of talking across process boundaries. Chromium's Mojo library:
message pipes, interfaces, and how auto-generated bindings keep C++ and the wire
format in sync.

**Chapter 4 – Life of a URL**
From the moment you press Enter to the moment pixels appear on screen. URL
parsing, DNS resolution, HTTP fetching, navigation commits, and renderer
lifecycle.

**Chapter 5 – The Networking Stack**
`//net` in detail. URLRequest, the socket pool, HTTP/2 and QUIC, the disk cache,
and the cookie store.

**Chapter 6 – The Renderer Process & Blink**
What lives in a renderer process and why. The Blink rendering engine's object
model, the DOM, and how JavaScript interacts with the platform.

**Chapter 7 – HTML, CSS Parsing and the Rendering Pipeline**
Tokenizing HTML, building the DOM tree, computing styles, the layout tree, paint,
and compositing layers.

**Chapter 8 – The JavaScript Engine (V8)**
How V8 compiles and executes JavaScript. The Ignition interpreter, the Turbofan
compiler, garbage collection, and the V8/Blink integration boundary.

**Chapter 9 – Security Architecture**
The threat model for a browser. The sandbox, site isolation, the same-origin
policy, CORS, content security policy, and safe browsing.

**Chapter 10 – GPU, Graphics & Compositing**
Why the browser has a GPU process. The compositing layer, viz (the display
compositor), and how a frame makes it from renderer memory to the screen.

**Chapter 11 – Memory Management**
PartitionAlloc, shared memory, memory pressure signals, and the tooling Chromium
uses to find and prevent memory safety bugs.

**Chapter 12 – Browser UI, Navigation & Sessions**
The Views toolkit, the omnibox, tab management, session restore, and how the
browser-side UI connects to the content layer.

**Chapter 13 – Contributing to Chromium**
Getting a checkout, building in component mode, running tests, the code review
process with Gerrit, and how the project is governed.
