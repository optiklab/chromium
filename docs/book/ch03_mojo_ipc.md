# Chapter 3: Mojo IPC – How Processes Talk to Each Other

> **Series**: The Chromium Architecture Book  
> **Audience**: First-year university students with basic C++ knowledge  
> **Prerequisites**: Chapter 1 (Introduction to Chromium), Chapter 2 (Multi-Process Architecture & Process Model)

---

## 3.1 Why We Need IPC

In Chapter 2 we learned that Chromium deliberately isolates renderer processes
inside a sandbox. A sandboxed process cannot read files, open sockets, or
allocate certain types of memory. This isolation is the whole point — a
compromised renderer can't reach out and harm the rest of your system.

But isolation creates a problem: the renderer *does* need things from the
outside world. It needs to load resources over the network, store data in
cookies, paint pixels to the screen, and ask the user for permission to use the
microphone. All of these operations must be performed by a *more-trusted*
process (usually the browser process), on behalf of the renderer.

The solution is **Inter-Process Communication (IPC)**: a structured way for
processes to send messages to each other across process boundaries.

### 3.1.1 Before Mojo: The Legacy IPC System

Chromium originally used a hand-rolled IPC system based on `base::Pickle` — a
simple serializer that stuffed typed values into a byte buffer. Messages were
defined using C preprocessor macros like `IPC_MESSAGE_ROUTED2(...)`.

This worked, but it had serious drawbacks:

| Problem | Description |
|---|---|
| **No type safety** | The receiver had to manually unpack fields in the right order |
| **No reply semantics** | Asynchronous replies required manual bookkeeping with "request IDs" |
| **Global message ordering** | A single FIFO channel for *all* messages between two processes created ordering bottlenecks |
| **Hard to audit** | Thousands of opaque `IPC_MESSAGE_*` macros scattered across the codebase |

Mojo was designed to replace all of this. The conversion is still ongoing, but
all *new* IPC code uses Mojo exclusively.

---

## 3.2 Mojo Fundamentals

Mojo is a collection of libraries providing a platform-agnostic abstraction of
IPC primitives, a message definition language, and generated language bindings.
Compared to legacy IPC, Mojo calls are approximately **1/3 faster** and use
**1/3 fewer context switches**.

### 3.2.1 Message Pipes

The most fundamental primitive in Mojo is the **message pipe**. A message pipe
is a pair of endpoints. Writing a message on one endpoint enqueues it on the
other (called the **peer**). Because both ends can send and receive, pipes are
**bidirectional**.

```
  Process A                     Process B
  ─────────                     ─────────
  [Endpoint 0] ←──messages──→ [Endpoint 1]
```

Creating a message pipe is cheap — essentially generating two random numbers
and making a few small heap allocations. You can create millions of them if you
need to.

### 3.2.2 Mojom: The Interface Definition Language

Raw message pipes carry untyped bytes. To give them structure, Mojo uses
**mojom files** — an Interface Definition Language (IDL) that describes
interfaces as strongly-typed collections of messages.

```
// example.mojom
module example.mojom;

interface Greeter {
  // Ask for a greeting; the => means "reply with".
  SayHello(string name) => (string greeting);
};
```

The build system reads `.mojom` files and **generates C++ bindings**
automatically. You never write serialization code by hand.

### 3.2.3 Remote\<T\> and Receiver\<T\>

Given a mojom interface `T`, one endpoint of a pipe is designated a
**`Remote<T>`** and the other a **`Receiver<T>`**:

- **`Remote<T>`** — the *calling* end. You invoke methods on it just like a
  C++ object. Mojo serialises the call and sends it across the pipe.
- **`Receiver<T>`** — the *implementing* end. It must be bound to a concrete
  C++ object that implements the interface. Incoming messages are dispatched as
  scheduled tasks on that object's thread.
- **`PendingReceiver<T>`** — an *unbound* receiver endpoint. It is an inert
  holder of a pipe endpoint that hasn't been given an implementation yet. It
  exists to carry strong typing across the boundary before being handed off.

Think of `Remote<T>` as a phone handset and `Receiver<T>` as the phone on the
other desk. `PendingReceiver<T>` is an unconnected handset still in its box,
waiting to be plugged in.

```
  Renderer Process                    Browser Process
  ─────────────────                   ───────────────
  Remote<Greeter>   ──[pipe]──→   PendingReceiver<Greeter>
        │                                  │
   (sends calls)              (bound to GreeterImpl)
                                      Receiver<Greeter>
```

---

## 3.3 End-to-End Example: Ping from Renderer to Browser

This walkthrough follows the exact pattern used in real Chromium code. We will
build a `Pingable` interface that lets a renderer send a Ping and receive a
random integer back.

### Step 1 – Define the Interface

Create a `.mojom` file describing the interface:

```
// src/example/public/mojom/pingable.mojom
module example.mojom;

interface Pingable {
  // Receives a Ping and responds with a random integer.
  Ping() => (int32 random);
};
```

### Step 2 – Add a GN Build Rule

Tell the build system to generate C++ bindings:

```python
# src/example/public/mojom/BUILD.gn
import("//mojo/public/tools/bindings/mojom.gni")

mojom("mojom") {
  sources = [ "pingable.mojom" ]
}
```

Running `gn gen` followed by `ninja` will produce a
`pingable.mojom.h` header with all the C++ classes you need.

### Step 3 – Renderer Creates a Remote and a PendingReceiver

In the renderer, the *client* creates both ends of the pipe:

```cpp
// In renderer code (e.g., a Blink frame):
mojo::Remote<example::mojom::Pingable> pingable;
mojo::PendingReceiver<example::mojom::Pingable> receiver =
    pingable.BindNewPipeAndPassReceiver();
```

`BindNewPipeAndPassReceiver()` creates the underlying message pipe, assigns
`pingable` ownership of the `Remote` end, and returns the `PendingReceiver`
end. At this point the `Remote` is already usable — you can queue calls
immediately even before the other end is bound.

### Step 4 – Send the PendingReceiver to the Browser

`PendingReceiver` objects are just typed pipe endpoints; they can be sent
across *other* already-connected Mojo interfaces. The standard mechanism for
renderer→browser communication is `BrowserInterfaceBroker`:

```cpp
RenderFrame* my_frame = GetMyFrame();
my_frame->GetBrowserInterfaceBroker().GetInterface(std::move(receiver));
```

`GetInterface` accepts a `GenericPendingReceiver` (which `PendingReceiver<T>`
converts to implicitly) and transfers the endpoint to the browser process.

### Step 5 – Browser Implements the Interface

```cpp
// browser_side_impl.cc
#include "example/public/mojom/pingable.mojom.h"

class PingableImpl : public example::mojom::Pingable {
 public:
  explicit PingableImpl(
      mojo::PendingReceiver<example::mojom::Pingable> receiver)
      : receiver_(this, std::move(receiver)) {}
  PingableImpl(const PingableImpl&) = delete;
  PingableImpl& operator=(const PingableImpl&) = delete;

  // example::mojom::Pingable:
  void Ping(PingCallback callback) override {
    std::move(callback).Run(4);  // Chosen by fair dice roll.
  }

 private:
  mojo::Receiver<example::mojom::Pingable> receiver_;
};
```

The browser-side handler is registered in `browser_interface_binders.cc`:

```cpp
void PopulateFrameBinders(RenderFrameHostImpl* host, mojo::BinderMap* map) {
  map->Add<example::mojom::Pingable>(base::BindRepeating(
      &RenderFrameHostImpl::GetPingable, base::Unretained(host)));
}
```

### Step 6 – Renderer Calls the Method

```cpp
// Back in the renderer:
pingable->Ping(base::BindOnce(&OnPong));

void OnPong(int32_t random_value) {
  // random_value == 4 (very random)
}
```

**Important:** The `pingable` object *owns* its pipe endpoint. If it goes out
of scope before `OnPong` is called, the pipe is destroyed and the reply is
silently dropped. Keep it alive.

### Full Message Flow Diagram

```
  Renderer Process                           Browser Process
  ─────────────────                          ───────────────
  pingable->Ping(cb)
       │
       │  [Mojo serializes call]
       │─────────────────────────────────────→ PingableImpl::Ping()
                                                     │
                                          std::move(callback).Run(4)
                                                     │
       ←─────────────────────────────────────────────│
  OnPong(4) invoked
```

---

## 3.4 BrowserInterfaceBroker

**`BrowserInterfaceBroker`** is the standard "phone book" that renderer frames
use to acquire browser-side interfaces. Its mojom definition is simple:

```
interface BrowserInterfaceBroker {
  GetInterface(mojo_base.mojom.GenericPendingReceiver receiver);
};
```

Every `RenderFrameImpl` in the renderer is permanently connected to a
corresponding `BrowserInterfaceBroker` implementation in the browser process.
When the renderer calls `GetInterface(receiver)`, the broker looks up a
registered handler for that specific interface type and routes the
`PendingReceiver` to it.

The broker is strongly associated with a single origin — when a frame navigates,
the old broker is torn down and a new one is created for the new origin. This
means browser-side interface handlers can safely assume they are operating on
behalf of the correct origin without having to re-validate it from an IPC
argument.

---

## 3.5 Service Architecture

Renderer-to-browser messaging is only part of the picture. Chromium is also
decomposed into semi-independent **services** — self-contained libraries whose
only public API is a Mojo interface. Examples include:

| Service | What it does |
|---|---|
| **Network Service** | All network I/O (HTTP, WebSockets, DNS) |
| **Storage Service** | Databases, local storage, file-system access |
| **Audio Service** | Low-level audio mixing and device access |
| **Video Capture Service** | Camera access |

Each service defines a top-level Mojo interface (e.g., `network.mojom.NetworkService`)
and can run either in-process (for debugging) or in a separate, sandboxed utility
process. The browser process acts as a broker, launching service processes on
demand and holding the `Remote<>` handles to each one.

```
  Browser Process
  ───────────────
  Remote<NetworkService> ──→ [Network Service Process]
  Remote<StorageService> ──→ [Storage Service Process]
  Remote<AudioService>   ──→ [Audio Service Process]
       ↑
  Renderer requests go through the browser, never directly to services
```

This architecture provides **privilege separation at the interface boundary**:
a compromised renderer cannot speak directly to the network or storage services
because it never holds those `Remote<>` handles. All requests must pass through
browser-side code that can apply policy.

---

## 3.6 Security Considerations

Mojo gives Chromium's security model a firm foundation — but the interface
boundary only provides safety if the browser-side implementation actively
enforces it.

### 3.6.1 Never Trust Renderer-Side Data

The renderer is sandboxed precisely *because* it is untrusted. Its process may
be running attacker-controlled JavaScript. Any data it sends over Mojo must be
treated as potentially malicious:

- **Do not trust origin claims.** The browser process already knows what origin
  a frame is on (`RenderFrameHost::GetLastCommittedOrigin()`). Never accept an
  origin sent as an IPC argument from a renderer — a compromised renderer could
  send any origin it likes.
- **Do not trust file paths.** Allowing a renderer to supply a path like
  `~/.bashrc` to a file-write operation would be catastrophic. Validate,
  sanitise, or construct paths entirely in the browser process.
- **Validate all offsets, sizes, and indices.** Assume arithmetic can overflow
  and that every value is at its extreme.

### 3.6.2 Mojo as the Security Boundary

The Mojo interface definition *is* the security boundary. Every method argument
is a potential attack surface. Chromium's security team conducts **IPC Reviews**
for all new or changed Mojo interfaces, specifically looking for:

- Methods that accept privilege-presuming data (origins, capabilities, paths)
- Methods that grant capabilities without proper authentication
- Interfaces that are over-exposed (available to all frames when they should be
  restricted to privileged frames only)

The ideal flow for any privileged operation is:

1. Renderer asks the browser for a *capability* (not raw data).
2. Browser applies policy and decides whether to grant it.
3. Browser hands back a scoped Mojo interface representing only that
   capability — nothing more.

### 3.6.3 Common Mistakes

```
// BAD: Renderer claims its own origin — never do this.
interface ResourceLoader {
  FetchResource(url.mojom.Url url, url.mojom.Origin claimed_origin) => (...);
};

// GOOD: Browser derives origin from the frame's committed navigation state.
interface ResourceLoader {
  FetchResource(url.mojom.Url url) => (...);
  // Browser looks up origin via RenderFrameHost::GetLastCommittedOrigin().
};
```

### 3.6.4 Strive for Simple Interfaces

Complexity is the enemy of security. Interfaces that require callers to invoke
methods in a specific order, or that have intricate cross-method state, are hard
to reason about and easy to exploit. The Mojo style guide recommends:

- Keep interfaces focused on a single responsibility.
- Avoid optional fields that imply ambiguous call patterns.
- Document preconditions, postconditions, and which process each side lives in.

---

## 3.7 Key Takeaways for Contributors

If you are writing new Chromium code that crosses a process boundary, here is
the condensed advice from this chapter:

1. **Use Mojo, not legacy IPC.** Legacy `IPC_MESSAGE_*` macros exist only for
   historical reasons. All new cross-process communication must use Mojo
   interfaces.

2. **Define a `.mojom` interface first.** Writing the interface definition
   before the implementation forces you to think about the public contract,
   what data crosses the boundary, and who owns what.

3. **Validate everything in the browser process.** The browser is the policy
   enforcement point. Never act on data from a less-trusted process without
   checking it against state the browser itself holds.

4. **Never send privilege-presuming data from a renderer.** If the browser
   already knows the origin, the URL, or the permission state — and it almost
   always does — don't ask the renderer to re-supply it.

5. **Expect an IPC review.** Security reviewers will examine every new or
   modified Mojo interface. Write clear comments, document trust boundaries, and
   be prepared to justify every method argument.

6. **Keep the `Remote<>` alive.** If you are waiting for a reply callback,
   the `Remote` must remain in scope or the pipe will be destroyed and your
   callback will never run.

---

## Summary

| Concept | One-line description |
|---|---|
| **Message pipe** | Bidirectional pair of endpoints for transporting messages |
| **Mojom** | IDL that generates strongly-typed C++ bindings |
| **`Remote<T>`** | Calling end of a Mojo interface |
| **`Receiver<T>`** | Implementing end, dispatches calls to a C++ object |
| **`PendingReceiver<T>`** | Unbound receiver endpoint, safe to pass between processes |
| **`BrowserInterfaceBroker`** | Per-frame "phone book" for renderer→browser interfaces |
| **Service** | Self-contained feature library whose only API is Mojo |
| **IPC Review** | Security team audit of all new/changed Mojo interfaces |

Mojo turns the hard problem of safe cross-process communication into a
straightforward problem of writing an interface definition, generating bindings,
and then implementing a C++ class. The discipline required — always validate in
the browser, never trust renderer claims — is what keeps the multi-process
security model intact.

---

*Next: Chapter 4 – The Renderer Process and Blink*
