# Chapter 6: The Renderer Process & Blink

## Introduction

In previous chapters we looked at how the browser process acts as the trusted
supervisor of Chrome. This chapter dives into the *renderer process* — the part
of Chrome that actually interprets and displays web content. Understanding the
renderer means understanding Blink, Chrome's rendering engine, as well as the
V8 JavaScript engine, the threading model, and the memory management system that
keeps everything running safely and efficiently.

---

## 6.1 What the Renderer Process Does

When you navigate to a web page, the browser process spawns (or reuses) a
renderer process and hands it the raw bytes of the HTML document. From that
point on, the renderer is responsible for everything you see on screen:

- **HTML parsing** — turning bytes into a tree of DOM nodes
- **CSS parsing** — turning style sheets into computed styles
- **JavaScript execution** — running scripts via the V8 engine
- **Layout** — calculating where every element sits on the page
- **Painting** — producing drawing commands that describe how to fill pixels
- **Compositing** — assembling painted layers and sending GPU commands

```
Renderer Process
┌──────────────────────────────────────────────────┐
│  Blink Main Thread                                │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │  Parser  │ │  DOM     │ │  JavaScript (V8) │ │
│  │(HTML/CSS)│ │  Tree    │ │                  │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
│  ┌──────────────────────────────────────────────┐ │
│  │  Layout Engine (LayoutTree)                  │ │
│  └──────────────────────────────────────────────┘ │
│  Compositor Thread                               │
│  ┌──────────────────────────────────────────────┐ │
│  │  Layer Tree → Paint Records → GPU Commands   │ │
│  └──────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
```

### Multiple Renderers and Site Isolation

Chrome does not use a single renderer for everything. With *Site Isolation*
enabled (the default on desktop), each distinct site (scheme + host pair) gets
its own renderer process. This means that `https://example.com` and
`https://bank.com` are always kept in separate processes, even if they are open
in the same tab via an iframe.

```
Browser Process
 ├── Renderer A  (https://example.com  — main frame)
 ├── Renderer B  (https://ads.com      — cross-site iframe)
 └── Renderer C  (https://bank.com     — another tab)
```

Site Isolation is the primary defense against Spectre-class side-channel
attacks: even if malicious JavaScript reads stale CPU-cache data, it can only
see memory from its own process.

### The Renderer Is Sandboxed

Renderer processes run inside a strict OS-level sandbox. They have **no** direct
access to the file system, no ability to open network sockets, and no permission
to call most OS APIs. Any privileged action — reading a cookie, writing a
downloaded file, making a network request — must go through the browser process
via a Mojo IPC call. This design means that even if an attacker finds a bug in
the HTML parser or the JavaScript engine, they cannot escape to the OS without
also finding a separate bug in the browser process.

---

## 6.2 What Is Blink?

**Blink** is the rendering engine embedded inside the renderer process. Its
code lives at `//third_party/blink` in the Chromium source tree.

### A Short History

| Year | Event |
|------|-------|
| 1998 | KDE project creates **KHTML**, an open-source HTML engine for the Konqueror browser |
| 2003 | Apple forks KHTML to create **WebKit** for Safari |
| 2008 | Google ships Chrome, using WebKit as its rendering engine |
| 2013 | Google forks WebKit to create **Blink**, enabling faster independent development |

The fork allowed the Blink team to remove Apple-specific abstractions, simplify
the multi-process model, and experiment with features like Out-of-Process Iframes
(OOPIFs) without coordinating with Apple's Safari team.

### What Blink Handles

Blink is responsible for:

- **HTML parsing** — tokenizing the byte stream, building the DOM
- **CSS parsing** — computing styles, resolving the cascade
- **DOM** — the live tree of objects representing the document
- **Layout** — calculating box sizes and positions
- **Painting** — generating `PaintRecord` objects (drawing commands)

What Blink does *not* do alone:

- **JavaScript execution** — this is V8, a separate project that Blink embeds
- **GPU rendering** — handled by the compositor (`//cc`) and the GPU process
- **Networking** — handled by the browser/network process

### Directory Structure of `//third_party/blink/renderer`

```
renderer/
 ├── core/       ← The heart: DOM, CSS, layout, paint, frames, loaders
 ├── modules/    ← Self-contained Web Platform features (WebGL, WebRTC, …)
 ├── platform/   ← Lower-level abstractions (scheduler, GC/Oilpan, WTF, fonts)
 ├── bindings/   ← V8 ↔ Blink bridge: generated JS wrappers for DOM types
 └── extensions/ ← Embedder-specific, non-web-exposed APIs (e.g., ChromeOS)
```

The `core/` directory is the oldest and most interdependent part. `modules/` and
`platform/` are factored-out pieces that follow stricter dependency rules.

---

## 6.3 Key Blink Concepts

### 6.3.1 The Document Object Model (DOM)

The **DOM** is a tree of objects that mirrors the structure of an HTML document.
When the HTML parser encounters a tag it creates a corresponding `Node` object
in memory.

```
Document
 └── <html>  (HTMLHtmlElement)
      ├── <head>
      │    └── <title>  →  TextNode("My Page")
      └── <body>
           ├── <h1>  →  TextNode("Hello")
           └── <p>   →  TextNode("World")
```

Key node types:

- **`Document`** — the root; there is exactly one per browsing context
- **`Element`** — an HTML tag, e.g. `<div>`, `<img>`
- **`Text`** — a run of text between tags
- **`Attr`** — an attribute on an element (not a child node, but attached to
  `Element`)

**Shadow DOM** allows parts of the tree to be encapsulated — their internal
nodes are hidden from the outer document's JavaScript and CSS. This is used for
built-in controls like `<video>` (which has a hidden shadow tree for its play
button, progress bar, etc.) and is available to web developers for building
reusable components.

### 6.3.2 Frames and Windows

A **Frame** represents a *browsing context* — roughly, a viewport in which a
document is rendered. Every tab starts with one top-level frame; each `<iframe>`
element creates a child frame.

```
LocalFrame (top-level)   ←  renders https://example.com
 ├── LocalFrame           ←  same-site <iframe>
 └── RemoteFrame          ←  placeholder for a cross-site <iframe>
                              (real frame lives in another renderer process)
```

- **`LocalFrame`** — a frame whose document lives in *this* renderer process.
  It has a real DOM tree and runs JavaScript.
- **`RemoteFrame`** — a lightweight placeholder that represents a frame whose
  document lives in *another* renderer process. It knows the frame's position
  on screen so it can be painted as an empty rectangle for compositing, but
  it holds no DOM.

The **`Window`** object (a.k.a. `DOMWindow`) is the global JavaScript object
for a browsing context — when you write `window.location` in JavaScript, you are
accessing the `DOMWindow` of the current `LocalFrame`.

### 6.3.3 RenderFrameHost vs. RenderFrame

This pair of classes is one of the most important to understand when reading the
Chromium source.

```
Browser Process                         Renderer Process
┌─────────────────────────┐             ┌──────────────────────┐
│  FrameTreeNode          │             │                      │
│   └── RenderFrameHost ──┼──Mojo IPC──▶│  RenderFrame         │
│        (tracks frame,   │◀────────────┤  (actual impl,       │
│         manages         │             │   owns LocalFrame)   │
│         lifecycle)      │             │                      │
└─────────────────────────┘             └──────────────────────┘
```

- **`RenderFrameHost`** lives in the **browser process**. There is one per
  frame per document. It tracks security state, manages the frame's lifecycle,
  and mediates privileged operations.
- **`RenderFrame`** lives in the **renderer process**. It wraps Blink's
  `LocalFrame` and is the concrete implementation of the frame.
- They communicate exclusively through Mojo interfaces — `RenderFrameHost`
  calls methods on the renderer via a `mojom::Frame` remote, and the renderer
  calls back via a `mojom::FrameHost` remote.

**RenderDocument** is an ongoing architectural project. Historically a
`RenderFrameHost` might be *reused* across navigations when the page stayed on
the same site. The RenderDocument project makes `RenderFrameHost` strictly
one-to-one with a `Document`, so it is replaced on every navigation. This
eliminates entire categories of security bugs caused by stale per-frame state
leaking into a newly loaded page.

---

## 6.4 The Threading Model in Blink

Blink uses several threads inside the renderer process:

| Thread | Responsibility |
|--------|---------------|
| **Main thread** | All DOM operations, style, layout, JavaScript, paint |
| **Compositor thread** | Scrolling, CSS animations, handling input events |
| **Worker threads** | Web Workers, Service Workers, Worklets |
| **Media thread** | Audio/video decoding |

The **split between main thread and compositor thread** is crucial for
smoothness. When you scroll a page, the compositor thread can move layers
and produce a new frame at 60 fps without ever touching the main thread —
even if the main thread is busy running JavaScript.

```
Main Thread          Compositor Thread
──────────           ─────────────────
Parse HTML    ──▶    Receives LayerTree
Run JS               Handles scroll input
Layout         ──▶   Animates CSS transforms
Paint          ──▶   Sends tiles to GPU process
```

JavaScript *always* runs on the main thread (except in Workers), which is why
a long-running script freezes the page — it blocks layout and paint.

---

## 6.5 Memory Management: Oilpan

In the browser process, most objects are managed with raw C++ ownership
(`std::unique_ptr`, reference counting via `scoped_refptr`). In the renderer,
the DOM tree is managed differently because JavaScript makes ownership
unpredictably complex: a script might hold a reference to a node whose parent
has been removed, keeping the subtree alive indefinitely.

Blink solves this with **Oilpan**, a garbage collector (GC) built into the
renderer. Oilpan is a *tracing* GC: it periodically scans all live references
and frees objects that are no longer reachable.

### Using Oilpan in C++

```cpp
// Any class managed by Oilpan inherits from GarbageCollected<T>.
class MyNode : public GarbageCollected<MyNode> {
 public:
  void Trace(Visitor* visitor) const {
    visitor->Trace(child_);   // Tell the GC about all member pointers.
  }

 private:
  Member<MyNode> child_;      // Member<T>: a GC-traced pointer to another
                               // on-heap object. Never use raw T* for this.
};

// Allocate on the Oilpan heap (never use `new` directly).
MyNode* node = MakeGarbageCollected<MyNode>();
```

Key types:

- **`GarbageCollected<T>`** — base class; marks T as GC-managed
- **`Member<T>`** — a strong reference between two GC-managed objects; the GC
  follows these during marking
- **`Persistent<T>`** — a strong reference from *off-heap* code (e.g., a stack
  variable) into the GC heap; acts as a GC root
- **`WeakMember<T>`** — a weak reference; does not keep the target alive

Because Oilpan runs *concurrently* with JavaScript, it can collect DOM objects
without stopping script execution for long pauses. This is important for
interactive web applications.

---

## 6.6 V8 Integration

**V8** is Google's JavaScript engine. It runs inside the renderer process on the
main thread (per document). The Blink-V8 integration has two sides:

**V8 calls Blink** whenever a script touches a DOM property:
```javascript
document.getElementById("foo").style.color = "red";
```
V8 looks up `getElementById` on the `Document` wrapper object and calls the
generated C++ binding, which in turn calls `Document::getElementById()`.

**Blink calls V8** to fire events, run timers, and execute inline scripts found
in the HTML.

### Web IDL and Code Generation

The JavaScript API surface of the DOM is defined in **Web IDL** (Interface
Definition Language) files, one per interface:

```
third_party/blink/renderer/core/dom/document.idl
third_party/blink/renderer/core/html/html_element.idl
...
```

A code generator (`//third_party/blink/renderer/bindings/scripts/`) reads these
`.idl` files and produces C++ glue code that sits in
`//third_party/blink/renderer/bindings/`. This generated code:

1. Creates a V8 `FunctionTemplate` for each interface
2. Registers getter/setter callbacks for each attribute
3. Handles type conversion between V8 values (`v8::Value`) and C++ types
4. Manages the lifetime relationship between V8 wrapper objects and their
   underlying GC-managed Blink objects

This automation ensures that the JavaScript API always matches the spec (the
IDL files are derived directly from W3C/WHATWG specifications) and reduces
the risk of hand-written binding code introducing security bugs.

---

## 6.7 The Sandboxed Renderer in Practice

Because the renderer is sandboxed, any attempt to access external resources
must go through the browser process:

```
Renderer (untrusted)          Browser Process (trusted)
────────────────────          ─────────────────────────
fetch("https://...")   ──▶    URLLoaderFactory (network service)
localStorage.setItem() ──▶    DOMStorageHost
document.cookie        ──▶    CookieManager
```

**`SecurityOrigin`** is the object in Blink that tracks a document's *origin*
— the (scheme, host, port) triple. For example, `https://example.com:443`.
The renderer enforces the **Same-Origin Policy** (SOP) locally: if a script
from `https://a.com` tries to read `document.cookie` from an `<iframe>` loaded
from `https://b.com`, Blink checks their origins and blocks the access.

The browser process adds a second layer of enforcement: even if a compromised
renderer lies about its origin, the browser process verifies origin on every
privileged IPC call.

---

## 6.8 Renderer Process Lifecycle

### Creation

When the browser needs to load a new page:
1. The browser process checks whether a renderer already exists for the
   site (Site Isolation groups pages by site).
2. If not, it forks a new renderer process and passes it the URL to load.
3. A `RenderFrame` is created in the renderer; a `RenderFrameHost` is created
   in the browser to track it.

### Process Reuse and Swapping

For *same-site* navigations, the existing renderer process is reused. For
*cross-site* navigations (e.g., clicking a link from `news.com` to `bank.com`):
1. The browser creates a new `RenderFrameHost` in the *destination* renderer.
2. The old renderer paints its final frame.
3. The browser swaps which `RenderFrameHost` is active for that
   `FrameTreeNode`.
4. The old renderer's document is eventually torn down.

### Out-of-Process Iframes (OOPIFs)

When a page embeds a cross-site iframe, the iframe's content runs in a
*separate* renderer. The main frame's renderer holds a `RemoteFrame` placeholder
and communicates with the browser to stitch the two rendered surfaces together
at compositing time.

```
Tab (https://news.com)
 ├── Renderer A  →  main frame (news.com)
 │                  └── RemoteFrame placeholder for ads.com iframe
 └── Renderer B  →  ads.com iframe
                    └── LocalFrame with real DOM
```

### Crash and Recovery

If a renderer process crashes, the browser process detects this through the
Mojo connection dropping. Chrome displays the "Aw, Snap!" error page in the
affected tab or iframe. With the *RenderDocument* architecture, crash recovery
creates a fresh `RenderFrameHost` rather than attempting to reuse the old one,
preventing any stale state from carrying over.

---

## Summary

The renderer process is the workhorse of Chrome: it takes raw HTML, CSS, and
JavaScript and turns them into pixels on screen. Blink, forked from WebKit in
2013, provides the DOM, CSS engine, layout engine, and paint pipeline. V8,
integrated via generated IDL bindings, executes JavaScript. Oilpan, Blink's
garbage collector, manages the complex web of object references that the DOM
creates. The compositor thread keeps scrolling and animations smooth even when
JavaScript is busy. And the OS-level sandbox ensures that bugs in any of this
code cannot directly harm the user's system — any privileged operation must be
mediated by the trusted browser process via Mojo IPC.

| Concept | One-line summary |
|---------|-----------------|
| Blink | Rendering engine: DOM, CSS, layout, paint |
| V8 | JavaScript engine, runs on main thread |
| Oilpan | Blink's garbage collector for DOM objects |
| LocalFrame | A frame whose document lives in this renderer |
| RemoteFrame | A placeholder for a frame in another renderer |
| RenderFrameHost | Browser-side handle for a frame (trusted) |
| RenderFrame | Renderer-side frame implementation (untrusted) |
| Compositor thread | Handles scrolling/animations independently of JS |
| Site Isolation | One renderer process per site for security |
| OOPIF | Out-of-Process Iframe: cross-site iframes in separate renderers |
