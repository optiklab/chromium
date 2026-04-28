# Chapter 10: GPU, Graphics and Compositing

## Introduction

Every time you scroll a webpage, watch a video, or run a CSS animation, Chromium
performs a complex sequence of work that ends with pixels on your screen. This
chapter traces that journey from DOM changes all the way to display hardware,
explaining why the pipeline is structured the way it is and where the 16.7 ms
per-frame budget goes.

---

## 10.1 Why a Separate GPU Process?

GPU drivers have a long history of bugs. A driver crash historically meant a
full browser crash. Chromium avoids this by running all GPU work in a dedicated
**GPU process**. If the driver crashes, only that process dies; the browser can
restart it without losing tabs.

There is also a security reason. A compromised renderer process should not be
able to call GPU driver APIs directly—those APIs have enormous attack surface
and can escalate privileges. By routing all GPU calls through the GPU process,
Chromium can validate every command before it reaches the driver.

```
+------------------+     Mojo IPC     +----------------+
| Renderer Process | --------------> | GPU Process    |
| (one per site)   |                  | Skia / ANGLE   |
+------------------+                  | GPU Driver     |
                                       +----------------+
                                              |
                                         GPU Hardware
                                              |
                                          Display
```

The GPU process is shared by all renderer processes in the browser, acting as a
single gatekeeper for GPU access.

---

## 10.2 The Full Graphics Stack

Here is the complete path from a DOM change to a displayed pixel:

```
Renderer Process
  Blink: DOM  -->  Layout  -->  Paint Records (display lists)
        |
        v
  cc (compositor client)
    LayerTreeHost (main thread)
    LayerTreeHostImpl (compositor thread)
    Tiles: 256x256 or 512x512 pixel chunks
        |
        |  (Mojo command buffer)
        v
GPU Process
  Viz (display compositor)
    SurfaceAggregator: merges frames from all clients
  Skia: converts display lists to pixels
  ANGLE: translates OpenGL ES -> D3D11 / Metal / Vulkan
  GPU Driver
        |
        v
    SwapBuffers -> Display
```

Each layer of this stack has a distinct responsibility. Blink knows about the
web page; cc knows about layers and tiles; Skia knows about 2D drawing; ANGLE
knows about the native GPU API; Viz knows how to combine frames from many
sources.

---

## 10.3 The Compositor (cc)

### Layer Tree Concepts

Blink builds a *layer tree* that cc manages. The important layer types are:

- **PictureLayer** — holds a *display list* of paint commands recorded by Blink.
  Most content lives here.
- **TextureLayer** — wraps a GPU texture directly; used for `<video>`, `<canvas>`,
  and WebGL.
- **SurfaceLayer** — embeds a frame produced by another compositor client, used
  for out-of-process iframes (OOPIFs).

There are two mirror objects for each layer tree:

| Object | Thread | Role |
|---|---|---|
| `LayerTreeHost` | Main thread | Owned by Blink; updated during style/layout/paint |
| `LayerTreeHostImpl` | Compositor thread | Drives animation and drawing |

Keeping these on separate threads is the key to smooth scrolling: the compositor
thread can continue animating and scrolling even while the main thread is busy
running JavaScript.

### The Commit Cycle

```
Main Thread                       Compositor Thread
-----------                       -----------------
1. HandleInputEvents
2. Animate
3. StyleUpdate
4. LayoutUpdate
5. Paint (record display lists)
6. --- Commit ---  ------------> 7. Pending tree updated
                                  8. Activate (pending -> active)
                                  9. Draw (generate CompositorFrame)
                                 10. SubmitCompositorFrame -> Viz
```

A **commit** copies the layer tree from the main thread to the compositor
thread's *pending tree*. The main thread then unblocks immediately. When the
pending tree is ready (tiles rasterized), it *activates*, becoming the *active
tree* that the compositor draws from.

### Tiling

Large layers are divided into **tiles** (typically 256×256 or 512×512 pixels).
This matters for two reasons:

1. **Memory**: Only visible tiles need to be in GPU memory.
2. **Prioritization**: Tiles nearest the viewport are rasterized first, so the
   user sees content sooner when scrolling fast.

When tiles are not yet rasterized, the browser shows a gray or checker pattern
called **checkerboarding**. It looks bad, which is why tile prioritization
exists.

### Compositor-Thread Animations

CSS `opacity`, `transform`, and `filter` can all be animated entirely on the
compositor thread without touching the main thread. This is why these properties
are the preferred choice for smooth animations:

```
opacity: 0 -> 1        ✓  Compositor thread only (fast)
transform: translateX  ✓  Compositor thread only (fast)
width: 100px -> 200px  ✗  Requires layout on main thread (slow)
background-color       ✗  Requires paint on main thread (slow)
```

The CSS `will-change` property is a hint to Chromium that an element will
animate. It causes Chromium to promote the element to its own `PictureLayer`
early, avoiding the cost of layer promotion when the animation starts.

---

## 10.4 Rasterization

**Rasterization** converts a display list (a list of drawing commands like
"draw a blue rectangle at 10,20 with size 50×30") into actual pixel values.

Chromium uses **GPU rasterization** by default: Skia submits OpenGL or Vulkan
draw calls to execute the rasterization on the GPU, which is much faster than
doing it on the CPU for complex content.

With **OOP-R (Out-of-Process Rasterization)**, rasterization happens inside the
GPU process rather than the renderer process. The renderer sends the display
list over Mojo, and Skia in the GPU process executes it. This improves security
isolation and reduces memory duplication.

Tile worker threads execute raster tasks in parallel. The scheduler prioritizes
tiles in the visible viewport, then nearby tiles, then off-screen tiles.

---

## 10.5 The VSync-Driven Frame Pipeline

Displays refresh at a fixed rate—typically 60 Hz, meaning one new frame every
16.7 ms. Chromium's scheduler is built around this cadence:

```
VSync signal (every 16.7 ms)
  |
  v
BeginImplFrame  [compositor thread]
  |
  v
BeginMainFrame  [main thread]
  |  HandleInputEvents
  |  Animate
  |  StyleUpdate
  |  LayoutUpdate
  |  Paint (record display lists)
  |
  v
Commit  [main -> compositor]
  |
  v
Activate + Raster  [compositor thread + GPU]
  |
  v
SubmitCompositorFrame  [compositor -> Viz in GPU process]
  |
  v
SurfaceAggregation + GPU Draw  [GPU process]
  |
  v
SwapBuffers -> Display
```

The scheduler uses **deadline-based scheduling**: the main thread is given a
deadline (roughly halfway through the frame budget). If it finishes in time, its
updated layer tree is committed before the compositor draws. If it misses the
deadline, the compositor draws the *previous* active tree anyway—this preserves
scroll smoothness at the cost of showing slightly stale content for one frame.

A **dropped frame** (jank) occurs when neither path completes before the vsync
deadline.

---

## 10.6 Viz: The Display Compositor

**Viz** (short for *visuals*) runs inside the GPU process and is responsible for
assembling the final image shown on screen.

Each compositor client (a renderer, a browser UI, an OOPIF) submits a
`CompositorFrame` to Viz. A `CompositorFrame` contains:
- A list of **render passes** (groups of quads describing what to draw)
- References to **resources** (textures, display lists)

The **SurfaceAggregator** combines these frames into a single render pass list.
For example, a page with a cross-origin iframe has two separate surfaces; Viz
merges them before drawing.

Viz also handles **hardware overlays**: on supported hardware, video frames can
be placed directly in a hardware overlay plane, bypassing the compositor draw
entirely. This saves significant GPU power for video playback.

---

## 10.7 Skia

**Skia** is the 2D graphics library at the heart of Chromium's rendering. It
provides:

- `SkCanvas` — the drawing surface (analogous to an HTML `<canvas>`)
- `SkPaint` — describes how to draw (color, shader, blend mode)
- `SkBitmap` — a CPU-side pixel buffer

For threaded rasterization, Skia uses **Deferred Display Lists (DDL)**. A
display list is recorded on one thread without touching GPU state, then replayed
on a GPU thread. This lets tile worker threads record drawing commands in
parallel and then flush them to the GPU in batch.

Skia backends used by Chromium include:
- **Ganesh** — the OpenGL/Vulkan GPU backend (current default)
- **Graphite** — a newer, lower-overhead GPU backend under active development

---

## 10.8 ANGLE

OpenGL ES is a cross-platform graphics API, but its implementations vary
wildly across vendors. **ANGLE** (Almost Native Graphics Layer Engine) solves
this by translating OpenGL ES calls into the native graphics API for each
platform:

| Platform | ANGLE translates to |
|---|---|
| Windows | Direct3D 11 |
| macOS | Metal |
| Linux | Vulkan or native OpenGL |
| Android | Vulkan or native OpenGL ES |

This gives Chromium consistent, tested behavior everywhere. The Direct3D and
Metal drivers are generally more stable and better tested than OpenGL drivers on
those platforms.

ANGLE also sits between WebGL/WebGPU and the driver, providing an additional
validation layer.

---

## 10.9 WebGL and WebGPU

**WebGL** exposes OpenGL ES 2.0 (WebGL 1) and OpenGL ES 3.0 (WebGL 2) to
JavaScript. Every WebGL call goes through a **command buffer**:

```
JavaScript (renderer)
  -> WebGL API call
  -> Serialize into command buffer
  -> Send to GPU process over Mojo
  -> GPU process validates each command
  -> ANGLE -> native GPU driver
```

Validation is critical: without it, a malicious page could send crafted GPU
commands that exploit driver bugs. The GPU process validates every command before
forwarding it.

**WebGPU** is a modern replacement for WebGL. It exposes concepts from Vulkan,
Metal, and Direct3D 12 (pipelines, bind groups, compute shaders) to JavaScript
via a clean API. WebGPU is also sandboxed through the GPU process with its own
command validation path (via the Dawn library).

---

## 10.10 The 60 fps Budget

Each frame has a budget of **16.7 ms** (1000 ms ÷ 60). The work that must fit
inside it includes:

```
[Main thread]           [GPU thread]
Input handling          Rasterize tiles
JavaScript              Execute render passes
Style recalc            ANGLE translation
Layout                  Driver submission
Paint recording         SwapBuffers
Commit
[Compositor thread]
Activate
Frame submission
```

Common causes of jank:
- Long JavaScript tasks blocking the main thread past the commit deadline
- Layout thrashing (reading then writing DOM geometry in a loop)
- Too many layers, each needing their own rasterization pass
- Large, un-tiled layers that must rasterize entirely before displaying

**Tools for diagnosis**:
- `chrome://tracing` — full timeline of all threads, including GPU process
- DevTools **Performance** tab — frame timeline with main thread breakdown
- DevTools **Layers** panel — visualizes the layer tree and tile state

**Checkerboarding** (gray squares where content should be) signals that tiles
were not ready by the draw deadline. This usually means rasterization is too
slow—too much content, too little GPU throughput, or over-use of layers.

---

## Summary

| Component | Where it runs | What it does |
|---|---|---|
| Blink paint | Renderer, main thread | Records display lists from DOM |
| cc / LayerTreeHost | Renderer, main thread | Manages layer tree |
| cc / LayerTreeHostImpl | Renderer, compositor thread | Drives animation and draw |
| Tile rasterization | Renderer workers + GPU process | Converts display lists to pixels |
| Viz SurfaceAggregator | GPU process | Combines frames from all clients |
| Skia | GPU process (and renderer) | 2D drawing primitives |
| ANGLE | GPU process | Translates OpenGL ES to native API |
| GPU driver | Kernel / hardware | Executes GPU commands |

The architecture is designed around one goal: keep the compositor thread free to
scroll and animate at 60 fps regardless of what the main thread is doing. Every
structural decision—separate processes, separate threads, tiling, deadline
scheduling, Viz aggregation—exists to protect that goal.

---

*Next: Chapter 11 — Networking and the Network Service*
