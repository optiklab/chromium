# Chapter 2: Multi-Process Architecture & Process Model

## 2.1 Why Multiple Processes?

Early web browsers were single-process applications. Every tab, every plugin,
and every piece of JavaScript all ran in the same operating system process. This
was fine when web pages were simple documents, but as the web evolved into a
platform for full applications — with complex JavaScript engines, video, 3D
graphics, and untrusted third-party code — the single-process model became a
serious liability.

The core problem is **fault isolation**. In a single process, any component that
crashes takes everything else down with it. A buggy plugin or a malformed web
page could freeze or kill the entire browser, losing all your open tabs. Worse,
a security vulnerability in any part of the renderer — the code that parses HTML
and runs JavaScript — gives an attacker complete access to the browser's full
privileges, including the user's files and system resources.

Chromium's answer, introduced with the very first release of Chrome in 2008, was
to put different web content into **separate operating system processes**. The
benefits are substantial:

- **Stability**: A renderer crash in one tab cannot affect others. The browser
  continues running and can show a "dead tab" page without losing your other
  work.
- **Security**: Each renderer process runs in a *sandbox* — a severely
  restricted OS environment with almost no privileges. Even if an attacker
  exploits a vulnerability in the renderer, they are trapped inside the sandbox
  and cannot reach the user's files or the rest of the system.
- **Performance**: Different sites can run JavaScript in parallel on separate
  CPU cores. A slow or infinite-looping script on one page does not block
  another page from updating its UI.

The cost is **memory overhead**: each process has its own copy of the JavaScript
engine, image decoders, and other runtime state. On a low-memory device this
matters, and Chromium adjusts its process model accordingly (more on this in
§2.3).

---

## 2.2 The Process Types

Chromium is not just split into "browser" and "renderer" — it has a whole family
of process types, each with a well-defined role:

```
┌─────────────────────────────────────────────────────────────┐
│                      Browser Process                        │
│   (UI Thread, IO Thread, Profile, Prefs, Bookmarks, etc.)  │
└──────────┬────────────────────┬────────────────────────────┘
           │  (Mojo IPC)        │  (Mojo IPC)
    ┌──────▼──────┐      ┌──────▼──────┐
    │  Renderer   │      │  Renderer   │   ← one per site (desktop)
    │  Process    │      │  Process    │
    │ (sandboxed) │      │ (sandboxed) │
    └─────────────┘      └─────────────┘
           │
    ┌──────▼──────┐      ┌──────────────┐      ┌───────────────┐
    │ GPU Process │      │ Network Svc  │      │Utility Process│
    │(sandboxed)  │      │  Process     │      │  (sandboxed)  │
    └─────────────┘      └──────────────┘      └───────────────┘
```

### Browser Process — "The Boss"

There is exactly **one** browser process per Chrome instance. It is the
privileged root of the entire system. Its responsibilities include:

- Drawing the browser UI (address bar, tabs, toolbar)
- Coordinating navigations and deciding which renderer process to use
- Owning user data: profiles, preferences, bookmarks, passwords, cookies
- Making privileged OS calls on behalf of sandboxed processes (file access,
  network requests before the network service existed, etc.)

Because the browser process is trusted and unsandboxed, it must be robust. If
it crashes, the entire browser goes away.

### Renderer Processes — Where Web Content Lives

Renderer processes host the **Blink** rendering engine and the **V8** JavaScript
engine. They parse HTML, execute JavaScript, compute CSS layouts, and produce
painted output that the GPU process can display. Crucially, renderer processes
run inside a sandbox with highly restricted OS permissions (see §2.4).

Under full Site Isolation (used on desktop), each renderer process is locked to
a single *site* (scheme + eTLD+1, e.g., `https://example.com`). An iframe from
`https://bank.com` embedded in `https://news.com` will render in a *different*
process from the top-level page — an out-of-process iframe (OOPIF).

### GPU Process

The GPU process handles all interaction with the graphics hardware. Renderer
processes produce display lists and GPU command buffers and send them over IPC
to the GPU process, which actually calls OpenGL/Vulkan/Metal/DirectX. This
isolates the GPU driver (historically a major source of crashes and security
bugs) from both the browser process and the renderer.

### Network Service Process

Networking — DNS resolution, TLS handshakes, HTTP requests — runs in a separate
**network service** process. This allows stricter sandboxing of networking code
and makes it possible to enforce policies like CORB (§2.3) at the process
boundary.

### Utility Processes

Chromium spawns short-lived or specialized utility processes for tasks such as
audio processing, printing, file type conversion (e.g., decoding a PDF), and
speech recognition. Each runs sandboxed with the minimum privileges it needs.

### Plugin Processes (Legacy)

The original design included separate processes for NPAPI plugins like Adobe
Flash. These are now effectively retired (NPAPI was removed in 2015; Flash
support ended in 2020), but the concept — isolating unreliable third-party
native code in its own process — was an early validation of the multi-process
architecture.

---

## 2.3 Site Isolation

Multi-process architecture gives you crash isolation, but it alone does not
fully prevent a *compromised* renderer from attacking other sites. Suppose an
attacker exploits a JavaScript engine bug and gains arbitrary code execution
inside a renderer process. If that renderer holds data from multiple sites —
your bank page and an attacker-controlled page — the attacker can steal the bank
page's DOM, cookies, and passwords.

**Site Isolation** closes this gap by ensuring that a renderer process is only
ever allowed to hold data from a single site.

### Key Abstractions

**SiteInfo** represents the *security principal* — the identity that determines
which data a process may access. On the web, this is typically a *site*: scheme
plus eTLD+1. For example, `https://mail.google.com` and `https://maps.google.com`
both belong to the site `https://google.com`. Pages from the same site can use
`document.domain` to synchronously access each other, so they must share a
process.

**SiteInstance** is the core unit of process assignment. Any two documents in
the same browsing context group (see below) with the same SiteInfo must share a
process, because they have synchronous access to each other's DOM and JavaScript
heap. Two tabs visiting the same site but with no `window.opener` relationship
get separate SiteInstances and therefore separate processes.

**BrowsingInstance** (the implementation of a *browsing context group*) is the
group of tabs and frames that can reach each other by name — frames within one
page, popups with `window.opener` references, etc. Within a BrowsingInstance,
each site gets exactly one SiteInstance.

### What Site Isolation Prevents

1. **Compromised renderer exfiltration**: A locked renderer cannot ask the
   browser process for data belonging to other sites, because the browser
   enforces this with `ChildProcessSecurityPolicy::CanAccessDataForOrigin`.
2. **Spectre/Meltdown-class attacks**: These attacks work by reading memory
   through CPU speculation side-channels. If attacker code and victim data (e.g.,
   a cross-site iframe's DOM) share the same process address space, Spectre can
   leak the victim's data. Site Isolation ensures cross-site data is *never* in
   the same address space as attacker code.
3. **Cross-Origin Read Blocking (CORB)**: Even before an attacker's renderer
   reads memory, CORB prevents sensitive cross-origin responses (HTML, XML, JSON)
   from ever being delivered to a renderer process that doesn't own their site.
   Images and media are still allowed for compatibility.

### Full vs. Partial Site Isolation

- **Full Site Isolation** (*site-per-process*) is used on desktop platforms
  (Windows, Mac, Linux, ChromeOS). Every site gets its own renderer process,
  including cross-site iframes.
- **Partial Site Isolation** is used on Chrome for Android with ≥ 2 GB RAM.
  High-value sites (sites where users have entered passwords, OAuth logins, etc.)
  get dedicated locked processes; other sites may share unlocked processes to
  conserve memory.
- **No Site Isolation** applies to low-memory Android (<2 GB RAM), Android
  WebView, and Chrome for iOS (which uses WebKit).

---

## 2.4 The Sandbox

Even with Site Isolation, a renderer process can be compromised. The sandbox is
the last line of defense: it restricts what a compromised renderer can actually
*do* on the system, regardless of what code it runs.

The sandbox uses OS-level security mechanisms — not emulation, not code patching —
to restrict a process's capabilities. On Linux this means seccomp-BPF filters
and namespaces; on macOS, sandbox profiles; on Windows, the mechanisms below.

### The Broker/Target Model (Windows)

The Windows sandbox uses exactly two roles:

- **Broker** (= the browser process): A privileged supervisor. It spawns
  sandboxed processes, hosts the sandbox policy engine, and performs
  policy-allowed OS calls *on behalf of* the target.
- **Target** (= renderer processes, GPU process, etc.): Runs all code to be
  sandboxed. It cannot make most Win32 API calls directly; instead, the sandbox
  library intercepts these calls and forwards them via a low-level IPC channel to
  the broker for policy evaluation and execution.

The broker always outlives its targets. If a target needs to open a file, it
asks the broker. The broker checks the policy, and either performs the call and
returns the result, or denies it.

### OS Restrictions on the Target

The Windows sandbox restricts a target process using four mechanisms:

1. **Restricted Token**: The process token has all SIDs set to "deny only" and
   zero privileges. Its integrity level is *Untrusted* — the lowest available.
   This makes it nearly impossible to access any OS resource, even files with
   otherwise-open ACLs.
2. **Job Object**: A Windows Job object enforces global restrictions: no
   clipboard access, no global hooks (`SetWindowsHookEx`), no desktop switching,
   no `SystemParametersInfo` calls, and limited ability to create child
   processes.
3. **Desktop Object**: The target runs on a separate, isolated Windows Desktop
   object, preventing interaction with the normal user desktop.
4. **Integrity Level**: Combined with the restricted token, integrity level
   *Untrusted* means most resources — including those belonging to the user — are
   inaccessible.

The sandbox follows the **principle of least privilege**: the sandboxed code
cannot read user files, write to the registry, access the network directly, or
make changes that persist after the process exits. A compromised renderer is
trapped.

---

## 2.5 Threading Model

Every Chrome process is heavily multi-threaded. The goal is to keep the browser
highly responsive — user interactions should feel instant even while heavy work
is being done in the background.

### Key Threads

```
Browser Process                          Renderer Process
─────────────────────────────────        ──────────────────────────────
BrowserThread::UI  ← main thread         Blink main thread ← JavaScript,
  (draws UI, handles user input,           DOM, CSS layout, HTML parsing
   coordinates navigations)
                                         Compositor thread ← smooth
BrowserThread::IO  ← IPC thread            scrolling, animations
  (all Mojo IPC arrives here;
   routes to appropriate thread)         IO thread ← IPC messages arrive

Thread Pool (N worker threads)           Thread Pool (N worker threads)
  (file I/O, encoding, etc.)              (image decoding, etc.)
```

The **UI thread** (BrowserThread::UI) must never be blocked. It is responsible
for responding to keyboard and mouse events and keeping the browser's UI
painting. A stall of even a few hundred milliseconds on the UI thread makes the
browser feel frozen. Therefore, any potentially slow operation — file I/O,
network requests, database access — must be posted to a worker thread or the
thread pool.

The **IO thread** (BrowserThread::IO) receives all incoming Mojo IPC messages.
It does minimal processing itself, routing messages to the thread on which the
relevant Mojo interface is bound.

### Tasks and Task Runners

Chromium does not use raw threads for most work. Instead, it uses a **task**
abstraction: a `base::OnceClosure` (a function pointer plus bound arguments,
created with `base::BindOnce`) that can be posted to a queue for later
execution.

```cpp
// Post work to the UI thread from anywhere:
content::GetUIThreadTaskRunner({})->PostTask(
    FROM_HERE, base::BindOnce(&MyClass::UpdateUI, weak_ptr_));

// Post work to the thread pool:
base::ThreadPool::PostTask(
    FROM_HERE, base::BindOnce(&DoExpensiveWork, data));
```

A **`base::TaskRunner`** is an interface that accepts posted tasks. The key
specializations are:

- **`base::SequencedTaskRunner`**: Tasks run one at a time in posting order.
  Each task sees the side effects of the previous one. Tasks may execute on
  different physical threads, but never concurrently.
- **`base::SingleThreadTaskRunner`**: Like a sequenced runner, but all tasks
  run on the same physical thread (e.g., the UI thread).

### Why Sequences Instead of Raw Threads

Chromium strongly prefers **sequences** (virtual threads) over managing physical
threads directly. A sequence guarantees ordering and mutual exclusion — the
invariants most code actually needs — without pinning work to a specific OS
thread. This enables the thread pool to schedule tasks efficiently across
available CPU cores.

Using a `SEQUENCE_CHECKER` member in a class and posting all its tasks to a
single `base::SequencedTaskRunner` is the idiomatic way to make a class
thread-safe in Chromium, without using locks.

---

## 2.6 Communication Between Processes

Separate processes cannot share memory, so they must communicate through IPC.
Chromium uses **Mojo**, its own IPC system, for all inter-process communication.
Chapter 3 covers Mojo in detail, but the essentials are:

- **Message Pipes**: Bidirectional communication channels between two endpoints,
  possibly in different processes.
- **Interfaces**: Mojo interfaces (defined in `.mojom` files) describe typed,
  versioned APIs that one process exposes to another. The Mojo toolchain
  generates C++ bindings so that cross-process calls look like ordinary method
  calls, except they are asynchronous.
- **Trusted Authority**: The browser process is always the trusted party. When a
  renderer needs a privileged operation (opening a file, navigating to a new URL,
  reading a cookie), it sends a Mojo request to the browser process. The browser
  validates the request against the renderer's declared origin before acting.

This design means the renderer's IPC surface is a security boundary: the browser
must never trust a renderer unconditionally, because a compromised renderer may
send arbitrary IPC messages. Site Isolation and the sandbox complement each other
— the sandbox limits what a compromised renderer can do with its own process, and
the browser's IPC validation limits what it can achieve by asking the browser to
act on its behalf.

---

## Summary

Chromium's multi-process architecture is a layered defence system:

| Layer | Mechanism | What it prevents |
|---|---|---|
| Process isolation | Separate OS processes | Crash in one tab kills all tabs |
| Site Isolation | One renderer per site | Cross-site data theft by compromised renderer |
| Sandbox | OS restrictions on renderer | Compromised renderer attacking the OS/user |
| IPC validation | Browser checks renderer requests | Compromised renderer abusing browser privileges |
| Threading | Sequences + task runners | UI stalls, data races |

Each layer assumes the layers below it may fail. The result is a browser that
can run untrusted code from millions of web sites with a strong security story
and acceptable stability.

In Chapter 3, we will look inside the Mojo IPC system that makes all this
inter-process communication possible.
