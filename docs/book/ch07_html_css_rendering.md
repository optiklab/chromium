# Chapter 7: HTML, CSS Parsing and the Rendering Pipeline

## Introduction

In Chapter 6 we saw that the renderer process is responsible for turning raw
HTML bytes into pixels. This chapter zooms all the way in to show *how* that
transformation actually happens — step by step, data structure by data
structure. By the end you will be able to follow a web page from the first byte
off the network to the final bitmaps displayed on your screen, and understand
where the performance bottlenecks live.

---

## 7.1 The Big Picture: Bytes to Pixels

Every rendered web page travels through the same sequence of stages. Chromium's
engineers call this the **critical rendering path**:

```
Network bytes (HTML)
      │
      ▼  Tokenization
  Tokens (start tag, end tag, text, …)
      │
      ▼  Tree construction
  DOM Tree
      │
      ▼  CSS parsing + Style calculation
  Render Tree  (DOM nodes + computed styles)
      │
      ▼  Layout
  Layout Tree  (every box has a position and size)
      │
      ▼  Paint  (record drawing commands)
  Paint Records  (display lists)
      │
      ▼  Rasterization  (execute commands → pixels)
  Bitmaps  (one per tile or layer)
      │
      ▼  Compositing  (combine layers)
  Compositor Frame
      │
      ▼  Submit to GPU process (Viz)
  Pixels on screen
```

Each stage feeds its output to the next. Crucially, the browser does *not* wait
for the entire HTML document to arrive before starting — parsing is incremental,
and the browser tries to show something on screen as early as possible.

---

## 7.2 HTML Parsing

### 7.2.1 Tokenization (Lexing)

The HTML parser's first job is **tokenization**: reading the raw stream of
characters and grouping them into meaningful units called *tokens*. Tokens
represent things like:

- A **start tag** — `<p class="intro">`
- An **end tag** — `</p>`
- **Character data** (text) — `Hello, world`
- A **comment** — `<!-- note -->`
- A **DOCTYPE declaration** — `<!DOCTYPE html>`

Unlike most programming languages, HTML does not have a simple regular grammar.
The [HTML Living Standard](https://html.spec.whatwg.org/) specifies a **state
machine** with around 80 states. Each character read by the tokenizer can cause
a state transition, token emission, or both. This design deliberately handles
malformed markup gracefully — a browser must never crash or show a blank page
just because a developer forgot to close a tag.

In Blink (Chromium's rendering engine), the tokenizer is implemented in
`third_party/blink/renderer/core/html/parser/html_tokenizer.cc`. The class
`HTMLTokenizer` maintains the current state and emits `HTMLToken` objects.

```
Character stream:  <  p     c  l  a  s  s  =  "  i  n  t  r  o  "  >
State machine:   [Data] → [Tag open] → [Tag name] → [Before attr name]
                       → [Attr name] → [Before attr value] → [Attr value]
                       → [After attr value quoted] → [Data]
                 Emits: StartTag { name="p", attrs={class:"intro"} }
```

### 7.2.2 Tree Construction

The stream of tokens is fed to a **tree builder** (`HTMLTreeBuilder` in Blink),
which produces the **Document Object Model (DOM)** — a tree of node objects:

| Node type      | Example                     | C++ class           |
|----------------|-----------------------------|---------------------|
| `Element`      | `<div>`, `<img>`            | `HTMLElement`       |
| `Text`         | `Hello, world`              | `Text`              |
| `Comment`      | `<!-- note -->`             | `Comment`           |
| `Document`     | The root of every page      | `Document`          |
| `DocumentType` | `<!DOCTYPE html>`           | `DocumentType`      |

The tree builder maintains an **open-elements stack** and an **active
formatting elements list** to handle nesting rules. When it encounters a
mismatched or out-of-order tag (e.g., `<b><i></b></i>`), it runs the
**adoption agency algorithm** — a carefully specified procedure that makes sure
the DOM ends up well-formed no matter how broken the input is.

```
Input HTML:        <html><body><p>Hello <b>world</b></p></body></html>

Resulting DOM:
    Document
    └── html (HTMLHtmlElement)
        └── body (HTMLBodyElement)
            └── p (HTMLParagraphElement)
                ├── "Hello " (Text)
                └── b (HTMLElement)
                    └── "world" (Text)
```

### 7.2.3 Incremental Parsing and Parser Blocking

The browser starts parsing as soon as the first bytes of HTML arrive — it does
**not** wait for the full document. This matters a lot for perceived page speed:
the earlier the browser can see your `<link rel="stylesheet">` or `<img>` tags,
the earlier it can start fetching those resources.

However, one construct brings parsing to a halt: a **synchronous `<script>`
tag**. When the parser encounters `<script src="app.js">`, it must:

1. Stop parsing HTML.
2. Fetch `app.js` (unless it is already cached).
3. Execute the script (scripts can call `document.write()` which may add more HTML).
4. Resume parsing.

This is why script tags near the top of the page make pages feel slow. Two HTML
attributes help break the blockage:

- **`async`** — fetch the script in parallel, execute it as soon as it arrives
  (order not guaranteed). Good for independent scripts.
- **`defer`** — fetch in parallel, execute *after* parsing is complete, in
  order. Usually the right choice for page scripts.

### 7.2.4 The Preload Scanner

Blink runs a lightweight **preload scanner** that races *ahead* of the main
parser, scanning the not-yet-parsed HTML for resource hints. Even before the
tree builder has reached a `<link>` or `<img>` tag, the preload scanner fires
off network requests for those resources. This hides network latency and is one
of the single biggest wins for real-world page load performance.

```
Main parser:    <html> <head> ← currently here, blocked on a script
Preload scanner: ... <link rel="stylesheet" href="style.css">  ← fetch!
                 ... <img src="hero.jpg">                      ← fetch!
                 ... <script src="app.js">                     ← fetch!
```

---

## 7.3 CSS Parsing and the CSSOM

### 7.3.1 CSS Tokenization and Parsing

Stylesheets go through a similar tokenize → parse → tree-build pipeline. The
**CSS tokenizer** produces tokens like selectors, property names, values,
punctuation, and at-rules (`@media`, `@keyframes`). Blink's CSS parser is in
`third_party/blink/renderer/core/css/parser/` and produces a hierarchy of
objects:

```
CSSStyleSheet
└── CSSRuleList
    ├── CSSStyleRule  ("p { color: red; }")
    │   ├── selector: "p"
    │   └── CSSStyleDeclaration
    │       └── CSSProperty { name: "color", value: "red" }
    └── CSSMediaRule  ("@media screen { … }")
        └── … (nested rules)
```

### 7.3.2 The Cascade and Style Calculation

CSS stands for *Cascading* Style Sheets — the cascade is the algorithm for
deciding which rule wins when multiple rules apply to the same element. Blink's
`StyleResolver` evaluates the cascade in this priority order (highest wins):

```
1. !important declarations
2. Inline styles  (style="…")
3. ID selectors   (#my-id)
4. Class / attribute / pseudo-class selectors  (.btn, [type=text], :hover)
5. Type / element selectors  (p, div, a)
6. Universal selector / inherited values  (*)
```

Within the same priority level, **specificity** (a three-part score: IDs,
classes, elements) breaks ties. If specificity is equal, the **later** rule in
source order wins.

After the cascade, the `StyleResolver` also resolves **inheritance** (e.g.,
`color` is inherited from parent to child unless overridden) and computes
**relative values** (e.g., `1.5em` → an absolute pixel value based on the
parent's font size).

The output is a **`ComputedStyle`** object attached to each element, containing
every CSS property resolved to its final, absolute value. Nothing in
`ComputedStyle` is relative or unresolved.

```
Element: <p class="intro" style="font-size: 18px">

Cascade input:
  browser default:  { color: black,  font-size: medium }
  stylesheet:       { color: #333,   line-height: 1.5 }    (class .intro)
  inline:           { font-size: 18px }

ComputedStyle output:
  { color: #333, font-size: 18px, line-height: 27px, … (all 300+ properties) }
```

---

## 7.4 Layout

Layout (historically called "reflow") is the stage where the browser figures
out the exact position and size of every element on the page.

### 7.4.1 The Layout Tree

Not every DOM node produces a visible box. The layout tree is a *separate* tree
containing only nodes that affect geometry:

- Nodes with `display: none` are absent entirely.
- `<head>`, `<script>`, `<style>` produce no layout objects.
- A single DOM element may generate *multiple* layout objects (e.g., a list
  item with a bullet point).

The layout tree contains `LayoutObject` subclasses:

| Layout object      | What it represents                           |
|--------------------|----------------------------------------------|
| `LayoutBlock`      | Block-level box (`div`, `p`, `section`)      |
| `LayoutInline`     | Inline-level box (`span`, `a`, `em`)         |
| `LayoutText`       | A run of text characters                     |
| `LayoutImage`      | A replaced element (`img`, `video`)          |
| `LayoutFlexibleBox`| A flex container                             |
| `LayoutGrid`       | A grid container                             |

### 7.4.2 The Box Model

Every block-level layout object is modelled as a CSS **box**:

```
┌──────────────────────────────────────────┐
│                 margin                   │
│  ┌────────────────────────────────────┐  │
│  │              border                │  │
│  │  ┌──────────────────────────────┐  │  │
│  │  │           padding            │  │  │
│  │  │  ┌────────────────────────┐  │  │  │
│  │  │  │       content area     │  │  │  │
│  │  │  │    (width × height)    │  │  │  │
│  │  │  └────────────────────────┘  │  │  │
│  │  └──────────────────────────────┘  │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

Layout walks the layout tree top-down, computing widths (constrained by the
parent) and then bottoms-up computing heights (driven by children). Inline
content is laid out using **line boxes**: words are placed left-to-right (in
LTR languages) and wrapped when they reach the container edge.

### 7.4.3 Flexbox, Grid, and LayoutNG

Modern CSS introduces layout algorithms beyond simple block/inline flow:

- **Flexbox** (`display: flex`) — one-dimensional alignment of children along a
  main axis and cross axis. Implemented in `LayoutFlexibleBox`.
- **Grid** (`display: grid`) — two-dimensional table-like layout.
  Implemented in `LayoutGrid`.

Chromium is in the process of migrating all layout to **LayoutNG** — a
redesigned layout engine written to be more correct, more testable, and to
support advanced features like fragmentation (multi-column, printing). LayoutNG
uses an immutable **fragment** model: instead of mutating layout objects in
place, each layout pass produces new `PhysicalFragment` objects that record the
result. This makes incremental relayout cleaner and enables better parallelism.

### 7.4.4 Incremental Layout

Layout is expensive: a full-page relayout can take tens of milliseconds on a
complex page. Blink avoids unnecessary work with **dirty flags**. When a CSS
property changes on one element (say, a hover effect widens a button), only
that element and its descendants are marked **layout-dirty**. The next layout
pass only recomputes dirty subtrees, leaving the rest untouched.

---

## 7.5 Painting

### 7.5.1 Paint Records and Display Lists

Rather than drawing directly to the screen, Blink's paint stage **records**
drawing commands into a data structure called a **display list** (also called
paint records or a `PaintArtifact`). A display list is a sequence of
instructions like:

```
DrawRect(x=10, y=20, w=100, h=50, color=#3498db)
DrawText(x=15, y=35, text="Click me", font=Arial 14px)
DrawImage(x=200, y=0, w=400, h=300, image=hero.jpg)
ClipRect(x=0, y=0, w=800, h=600)
```

Recording paint commands separately from executing them gives the browser
enormous flexibility: the same commands can be replayed on the GPU, on a
different thread, or in a different process — without re-running layout or style.

### 7.5.2 Stacking Contexts and Paint Layers

CSS properties like `z-index`, `opacity`, `transform`, `filter`, and `will-change`
create new **stacking contexts**, which control the order in which content is
painted and composited. Blink builds a `PaintLayer` tree mirroring these
stacking boundaries.

Some paint layers are promoted to **compositing layers** (backed by a GPU
texture). A layer gets its own texture when it:

- Has a CSS `transform` or `opacity` animation.
- Uses `will-change: transform` (a hint from the developer).
- Is an `<iframe>` or a `<video>` element (composited for isolation).
- Overlaps another composited layer in a way that requires correct ordering.

Compositing layer boundaries are important: changes inside a composited layer
(e.g., a CSS animation moving an element) can be handled entirely by the
**compositor thread** without touching the main thread, enabling smooth 60 fps
animations even when the main thread is busy.

### 7.5.3 Rasterization: Tiles and the GPU

Paint records need to be **rasterized** — the drawing commands are executed to
produce actual bitmap pixels. Rasterization can happen:

- **On the CPU** — slower but always available as a fallback.
- **On the GPU** (GPU rasterization) — much faster, uses the GPU's parallel
  processing ability via Skia/Ganesh (Chromium's 2D graphics library).

Large layers are split into **tiles** (typically 256×256 or 512×512 pixels) so
that only the tiles near the viewport need to be rasterized urgently. Tiles far
off-screen can be rasterized at lower priority or not at all, saving memory and
time.

```
Composited layer (e.g., a long article)
┌─────────────────────────┐
│  tile  │  tile  │  tile  │  ← viewport: rasterize NOW
├────────┼─────────┼───────┤
│  tile  │  tile  │  tile  │  ← nearby: rasterize soon
├────────┼─────────┼───────┤
│  tile  │  tile  │  tile  │  ← far off-screen: low priority
└─────────────────────────┘
```

---

## 7.6 The Compositor (cc)

### 7.6.1 What cc Does

The **Chromium Compositor** (`cc`) is the part of the browser that assembles
all the rasterized tiles and layers into a final frame and sends it to the
display. `cc` spans two objects:

- **`LayerTreeHost`** — lives on the main thread, mirrors the compositing layer
  tree updated by Blink.
- **`LayerTreeHostImpl`** — lives on the **compositor thread**, does the actual
  work of scroll, animation, rasterization, and frame submission.

The critical insight of cc is that the **compositor thread runs independently
of the main thread**. Scrolling, CSS `transform`/`opacity` animations, and
pinch-zoom can all be driven at 60 fps by the compositor thread even when the
main thread is occupied with a long JavaScript task. This is why Chrome can
still scroll smoothly on a slow page.

### 7.6.2 The Commit

When Blink finishes a style + layout + paint cycle, the results must be
transferred to the compositor. This is called the **commit**:

```
Main thread (Blink):
  1. Style update
  2. Layout
  3. Paint  →  produces updated LayerTreeHost
  4. Commit: copies layer tree to compositor thread  (main thread blocks briefly)

Compositor thread (cc):
  5. Receives pending tree
  6. Rasterizes new/changed tiles (may happen on raster threads or GPU)
  7. Activation: promotes pending tree → active tree
  8. Draws and submits compositor frame to Viz
```

The main thread blocks only during step 4 while the layer data is copied. After
that it is free to work on the next frame while the compositor independently
handles rasterization and drawing.

### 7.6.3 Frame Submission and Viz

The compositor submits a **CompositorFrame** to the **Viz** service (the
Display Compositor) running in the GPU process. A CompositorFrame is a
structured list of **quads** — rectangles with associated texture handles —
that describe how to assemble the final image.

Multiple renderer processes (one per tab, one per iframe in some cases) each
submit their own CompositorFrame identified by a **SurfaceID**. The
**SurfaceAggregator** in Viz recursively replaces `SurfaceQuads` (references to
other frames) with their contents, producing one combined frame that the
**DisplayCompositor** draws to the screen buffer.

```
Renderer process A (tab 1):  CompositorFrame ──┐
Renderer process B (tab 2):  CompositorFrame ──┼──► SurfaceAggregator
Renderer process C (iframe):  CompositorFrame ──┘          │
                                                            ▼
                                               Single aggregated frame
                                                            │
                                                            ▼
                                               GPU draw commands → swap → screen
```

### 7.6.4 The Full Frame Timeline

Chromium's frame pipeline maps closely to the hardware vsync signal (typically
16.7 ms for 60 Hz):

```
Vsync
  │
  ├─[1] BeginImplFrame ─────────────────── compositor thread wakes up
  ├─[2] Compositor-only updates (scroll, animations)
  ├─[3] BeginMainFrame ─────────────────── main thread: JS, style, layout, paint
  ├─[4] Commit ────────────────────────── layer tree copied to compositor
  ├─[5] Raster ────────────────────────── tiles rasterized (raster threads/GPU)
  ├─[6] Activation ────────────────────── pending tree → active tree
  ├─[7] Draw + SubmitCompositorFrame ───── quads sent to Viz/GPU process
  └─[8] GPU draw + Swap ────────────────── pixels appear on screen
```

If any step takes too long to finish before the vsync deadline, Chromium submits
the *previous* active tree and the frame's update is deferred to the next
vsync — resulting in a **dropped frame** (jank).

---

## 7.7 Fonts and Text

Text rendering involves a surprisingly deep stack. When Blink lays out a run of
text it must:

1. **Select a font face** — consult the CSS `font-family` list in order.
   If a character is not in the primary font, fall back to the next font in the
   list, and ultimately to system-defined Unicode fallback fonts.
2. **Shape the text** — use HarfBuzz (the open-source shaping engine embedded
   in Chromium) to map Unicode code points to glyph IDs, apply kerning, ligatures,
   and complex script rules (Arabic, Devanagari, etc.).
3. **Rasterize glyphs** — FreeType (on Linux/Android) or the platform font API
   (DirectWrite on Windows, CoreText on macOS) renders each glyph to a bitmap,
   optionally with hinting and sub-pixel anti-aliasing.
4. **Cache aggressively** — glyph bitmaps, shaped runs, and font metrics are all
   cached heavily because font operations are expensive.

---

## 7.8 Putting It All Together: A Worked Example

Imagine your browser loads this tiny HTML file:

```html
<!DOCTYPE html>
<html>
  <head>
    <link rel="stylesheet" href="style.css">
  </head>
  <body>
    <p class="intro">Hello, Chromium!</p>
  </body>
</html>
```

Here is what happens, in order:

| Step | What happens |
|------|-------------|
| 1 | Network bytes arrive; HTML tokenizer starts immediately |
| 2 | Preload scanner sees `style.css` → fires network request |
| 3 | Tree builder creates `Document → html → head → body → p` DOM |
| 4 | Parser encounters `<link>`: stylesheet fetch begins (or was already started by preload scanner) |
| 5 | `style.css` arrives; CSS parser builds CSSOM |
| 6 | `StyleResolver` merges browser defaults + `style.css` → `ComputedStyle` for each node |
| 7 | Layout computes the `<p>`'s width (= viewport width - margins) and height (= line height of text) |
| 8 | Paint records a `DrawText` command for "Hello, Chromium!" |
| 9 | Commit: layer tree sent to compositor thread |
| 10 | Raster thread executes `DrawText` → bitmap tile |
| 11 | Compositor assembles the tile into a `CompositorFrame` and submits to Viz |
| 12 | GPU draws the frame; pixels appear on screen |

---

## 7.9 Performance Implications

Understanding this pipeline explains many common web performance rules:

- **Avoid parser-blocking scripts** at the top of the page — they stall
  tokenization and delay step 2 onwards. Use `defer` or `async`.
- **Minimize style recalculation scope** — changing a class on the `<body>`
  element can invalidate computed styles for the entire page.
- **Avoid layout thrashing** — alternating reads (`offsetWidth`) and writes
  (`style.width = …`) in JavaScript forces multiple synchronous layouts per
  frame.
- **Promote animated elements to their own compositing layer** using
  `will-change: transform` — this lets the compositor animate them without a
  main-thread layout or paint.
- **Use CSS `transform` and `opacity` for animations** rather than `top`/`left`
  or `width` — `transform` and `opacity` can be handled entirely on the
  compositor thread.

---

## 7.10 Summary

The rendering pipeline transforms HTML bytes into screen pixels through a
carefully ordered sequence of stages:

1. **HTML tokenization** — a spec-defined state machine produces typed tokens.
2. **Tree construction** — tokens build a DOM, handling malformed HTML gracefully.
3. **CSS parsing + cascade** — stylesheets are parsed and resolved into per-element `ComputedStyle` objects.
4. **Layout** — the layout tree computes the position and size of every box.
5. **Paint** — drawing commands are recorded into display lists.
6. **Rasterization** — display lists are rendered into bitmap tiles (often on the GPU).
7. **Compositing (cc)** — tiles are assembled into a `CompositorFrame` and submitted to Viz.
8. **GPU draw + swap** — the GPU renders the final image to the framebuffer.

Two architectural decisions define Chrome's performance character: **incremental
parsing** (so the page starts appearing before the HTML is fully downloaded) and
the **compositor/main thread split** (so scrolling and animations stay smooth
even when JavaScript is running).

---

*Next: Chapter 8 — The JavaScript Engine (V8)*
