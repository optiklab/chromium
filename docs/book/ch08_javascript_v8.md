# Chapter 8: The JavaScript Engine (V8)

## Why Browsers Need a JavaScript Engine

When JavaScript was invented in 1995, it was a small scripting language meant
to add modest interactivity to web pages — form validation, hover effects, a
pop-up alert here and there. Today, entire applications are written in
JavaScript. React, Angular, Vue, and countless other frameworks ship hundreds
of thousands of lines of JS that run completely in the browser. Gmail, Figma,
and VS Code are all "websites" that feel like native desktop apps.

This shift put enormous pressure on browsers to execute JavaScript *fast*. A
naive approach — reading source code line by line and doing what it says —
works, but it's too slow for modern workloads. The answer is **Just-In-Time
(JIT) compilation**: translate JavaScript to native machine code at runtime,
right before it runs.

Chromium delegates all JavaScript execution to **V8**, a separate C++ library
originally developed by Google in 2008. V8 is also the engine that powers
Node.js, so the same core technology that runs server-side JavaScript also runs
in your browser tab.

---

## V8 Architecture: Tiered Compilation

V8 does not use a single strategy for all code. Instead it uses *tiered
compilation*: start cheap, then invest in optimization only for code that
actually runs a lot. Here is the full pipeline:

```
JavaScript Source Code
       │
       ▼  (Parser)
Abstract Syntax Tree (AST)
       │
       ▼  (Bytecode Compiler)
  Ignition Bytecode  ◄────────────────────────────────────┐
  (interpreted)                                           │
       │                                                  │ deoptimize
       │  function called many times ("hot")              │
       ▼  (Sparkplug)                                     │
  Baseline Compiled Code                                  │
       │  still hot                                       │
       ▼  (TurboFan)                                      │
  Optimized Machine Code ──── (assumption violated) ──────┘
```

Each tier trades off startup time against peak performance:

| Tier        | Speed to produce | Execution speed | When used |
|-------------|-----------------|-----------------|-----------|
| Ignition    | Instant          | Moderate        | All code, always first |
| Sparkplug   | Very fast        | Good            | Moderately hot code |
| TurboFan    | Slower           | Excellent       | Frequently called functions |

---

## Parsing JavaScript

Before V8 can do anything with JavaScript, it must *understand* it. Parsing
turns raw text into a data structure the engine can work with.

**Step 1 — Tokenization (lexing):** The source string is broken into *tokens*:
keywords (`function`, `if`, `return`), identifiers (`myVariable`), literals
(`42`, `"hello"`), and operators (`+`, `===`). Whitespace and comments are
discarded.

**Step 2 — AST construction:** Tokens are assembled into an **Abstract Syntax
Tree** (AST). Every node in the tree is a language construct. A function
declaration becomes a `FunctionDeclaration` node containing a list of
`Statement` children, each of which may contain `Expression` nodes, and so on.

**Step 3 — Scope analysis:** V8 walks the AST to determine which variable names
are in scope where. This is needed to correctly handle JavaScript's closure
rules.

**Lazy (pre-)parsing:** Parsing is expensive. Most web pages load many
functions that are *never called* on that page view. V8 uses *lazy parsing*: it
scans function bodies just enough to check syntax and find variable
declarations, but defers building a full AST until the function is actually
called. This dramatically speeds up page load.

---

## Ignition: The Bytecode Interpreter

After parsing, V8's **Ignition** component compiles the AST to a compact
*bytecode* — a sequence of simple instructions for a virtual register machine.

Example bytecodes (simplified):

```
LdaSmi [5]        // Load small integer 5 into the accumulator
Star r0           // Store accumulator into register r0
LdaSmi [3]        // Load 3 into accumulator
Add r0, [0]       // Add r0 to accumulator
Return            // Return accumulator value
```

Why interpret first instead of compiling to machine code right away?

- **Startup speed**: generating bytecode is much faster than running TurboFan.
  For functions that run once or twice, the overhead of full compilation would
  not be worth it.
- **Memory**: bytecode is compact — a single instruction is a few bytes versus
  dozens for native machine code.
- **Code caching**: Chromium can save compiled bytecode to disk and reload it
  on the next visit, skipping the parse-and-compile step entirely.

### Type Feedback Vectors

As Ignition runs, it collects *type feedback*: at every call site and operation
it records what types were actually seen. For example, it might notice that `+`
always receives two integers. This information is stored in a **feedback
vector** associated with each function and later handed to TurboFan as hints
for optimization.

---

## TurboFan: The Optimizing Compiler

When a function's call count crosses a threshold, V8 promotes it to
**TurboFan**, its full optimizing JIT compiler.

### Sea of Nodes IR

TurboFan represents the program internally as a **Sea of Nodes** graph — a
single directed graph where nodes are operations and edges are either data
dependencies or control-flow dependencies. This unified representation allows
powerful whole-function transformations that are hard in a traditional linear IR.

### Speculative Optimization

TurboFan's secret weapon is *speculation*. Rather than generating slow general
code that handles every possible type, it looks at the type feedback collected
by Ignition and *assumes* the types will stay the same. If feedback says `x`
was always a 32-bit integer, TurboFan emits a fast integer addition
instruction.

Every speculation is guarded by a **check**:

```
if (x is NOT a Smi integer) → deoptimize
x + y  // fast path
```

If the check fails at runtime — say, someone passes a string — V8
**deoptimizes**: it throws away the compiled code, falls back to Ignition
bytecode, and continues from where it left off. The function can be recompiled
later with updated type feedback.

### Other Optimizations

- **Inlining**: frequently called small functions are copied into the caller,
  eliminating call overhead and exposing more optimization opportunities.
- **Escape analysis**: if an object never "escapes" a function (no reference
  leaves the function), it can be allocated on the stack instead of the heap,
  reducing GC pressure.
- **Dead code elimination**: unreachable branches are removed.

---

## Garbage Collection

JavaScript programmers do not call `free()`. The runtime automatically reclaims
memory no longer reachable — this is **garbage collection (GC)**. V8 uses a
*generational* GC based on the empirical observation that most objects die
young.

```
       V8 Heap
┌───────────────────────────────────────┐
│  Young Generation (New Space)         │
│  ┌──────────┐   ┌──────────┐         │
│  │  From    │   │   To     │  ← Scavenger (fast, minor GC)
│  │  Space   │   │  Space   │         │
│  └──────────┘   └──────────┘         │
│                                       │
│  Old Generation (Old Space)           │
│  ┌───────────────────────────┐        │
│  │  Long-lived objects       │  ← Mark-Compact (slower, major GC)
│  └───────────────────────────┘        │
└───────────────────────────────────────┘
```

**Young generation (Scavenger):** New objects are allocated here. A minor GC
(called a *scavenge*) runs frequently and quickly: it copies *surviving* objects
to the "To" half of the space, then flips the halves. Objects that survive
several scavenges are *promoted* to the old generation.

**Old generation (Mark-Compact):** Objects here are longer-lived. A major GC
marks all reachable objects, then compacts them to eliminate fragmentation.
This is more expensive, so it runs less frequently.

**Reducing pauses:** A stop-the-world GC that freezes the page for 100ms is
unacceptable. V8 uses two techniques to spread GC work out:

- **Incremental marking**: instead of marking the entire heap in one shot, V8
  does small marking steps interleaved with JavaScript execution.
- **Concurrent marking**: marking runs on a background thread in parallel with
  JavaScript, with a small synchronization step at the end.

**Oilpan:** Blink (the rendering engine) has its own C++ garbage collector
called *Oilpan* for managing DOM objects and other C++ heap allocations.
Because JavaScript objects can hold references to DOM nodes and vice versa, V8
and Oilpan must cooperate at GC time to avoid dangling cross-heap pointers.

---

## JavaScript Execution Contexts

### V8 Isolate

A **V8 Isolate** is a completely independent instance of the V8 engine — its
own heap, its own GC, its own compiled code. Isolates *do not share memory* and
*cannot communicate directly*. In Chromium, each renderer process has one main
isolate running on the main thread.

### V8 Context

Within a single isolate there can be multiple **contexts**. A context provides
the global object (`window` in browsers) and its built-in properties. A page
with iframes may have one context per frame, all within the same isolate, so
they can share compiled code but have separate global scopes.

Context isolation is a critical security primitive: JavaScript in one context
cannot read variables from another context unless explicitly allowed through the
*same-origin policy* and structured bindings.

---

## WebAssembly

**WebAssembly (Wasm)** is a binary instruction format designed as a compilation
target for languages like C, C++, and Rust. A Wasm module loads and runs
significantly faster than equivalent JavaScript because it skips parsing and
type inference.

V8 compiles Wasm using two tiers:

- **Liftoff**: a fast, simple baseline compiler that generates code quickly so
  the module can start running with minimal delay.
- **TurboFan**: the same optimizing compiler used for JavaScript, applied to
  hot Wasm functions for peak throughput.

**Security:** Wasm is sandboxed. It runs inside V8 with no direct access to
memory outside its designated linear memory region, no OS syscalls, and no DOM
access (unless explicitly provided through JavaScript imports). From Chromium's
threat model, Wasm is treated identically to JavaScript: potentially malicious,
contained by V8's security model and the renderer sandbox.

---

## V8 and Blink Integration

V8 executes JavaScript; Blink owns the DOM. Bridging them requires a carefully
generated layer of glue code called **bindings**.

```
JS code: document.getElementById("main")
         │
         ▼
  V8 C++ wrapper (v8_element.cc)    ← auto-generated from IDL
         │
         ▼
  Blink C++ (Element::GetElementById)
         │
         ▼
  DOM tree (C++ objects)
```

The process starts with **Web IDL** (Interface Definition Language) files that
describe every browser API — `getElementById`, `fetch`, `addEventListener` —
in a language-neutral format. A Python code generator (`generate_bindings.py`
and the `bind_gen` package) reads the IDL database and emits matching C++ files
(`v8_*.h` / `v8_*.cc`). These files:

1. Register the method with V8 so that JavaScript can call it.
2. Unpack JavaScript arguments from V8 values into C++ types.
3. Call the real Blink implementation.
4. Pack the C++ return value back into a V8 JavaScript value.

This two-way bridge is performance-critical. Heavy DOM manipulation from
JavaScript incurs the cost of crossing this boundary on every call.

---

## Worker Threads

JavaScript's traditional execution model is *single-threaded* — one call stack,
one event loop. Workers break this constraint:

| Worker type     | DOM access | Lifetime            | Isolate |
|-----------------|-----------|---------------------|---------|
| Web Worker      | No         | Tab lifetime        | Own     |
| Service Worker  | No         | Background, persistent | Own  |
| Shared Worker   | No         | Shared across tabs  | Own     |

Each worker gets its **own V8 Isolate**. They communicate with the main thread
only via `postMessage()`, which serializes data, ensuring memory safety. Service
Workers additionally intercept network requests and handle push notifications,
enabling offline-capable Progressive Web Apps (PWAs).

---

## Performance Considerations

Understanding V8 helps explain common JavaScript performance advice:

**Keep types consistent.** If a variable holds an integer 99% of the time but
occasionally holds a string, TurboFan cannot speculate on its type and must
emit slower general code. A function that always receives the same types is
called *monomorphic*; one that sees many types is *polymorphic* and harder to
optimize.

**Minimize short-lived allocations.** Every object you create in a tight loop
adds GC pressure. Reusing objects or using typed arrays instead of plain
objects reduces scavenger work and keeps frame rates smooth.

**Avoid `eval()` and `with`.** Both make static analysis impossible: `eval()`
can introduce new variables, and `with` makes scope resolution unpredictable.
V8 must fall back to slower code paths for any function containing them.

**Avoid deoptimization loops.** If a function gets optimized, triggers a
deopt, gets re-optimized, and deoptimizes again repeatedly, V8 eventually marks
it as "not optimizable" (a *megamorphic* or *bailed-out* function). Chrome
DevTools' Performance panel and the `--trace-deopt` V8 flag can reveal this.

---

## Summary

V8 is an engine in the truest sense — a highly engineered system that turns
human-readable JavaScript into fast native code through a tiered pipeline:
parse → Ignition bytecode → Sparkplug baseline → TurboFan optimized code.
Its garbage collector keeps memory manageable without freezing the page, and
its tight integration with Blink through generated bindings makes every DOM API
call possible. Whether it's running a React application, executing a
WebAssembly game engine, or powering a background Service Worker, V8 is the
beating heart of the modern web experience inside Chromium.

---

*Next: Chapter 9 — Security Architecture: Sandboxing, Site Isolation, and the
Browser's Defense-in-Depth Model*
