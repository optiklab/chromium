# Chapter 11: Memory Management

## Why Memory Matters in a Browser

Open Chrome with twenty tabs and a system monitor. You will see dozens of
processes each consuming tens to hundreds of megabytes. Browsers are among the
most memory-hungry applications on a typical computer, and for good reason: they
are running entire programs — web pages — written by millions of developers, all
inside one application.

The multi-process architecture described in earlier chapters is a security and
stability win, but it is not free. Every renderer process needs its own copy of
the V8 JavaScript engine, its own DOM heap, its own decoded images, and its own
stack. Multiply that by the number of open tabs and the RAM bill adds up fast.

Memory also trades directly against speed. Caching a decoded image avoids
re-decoding it on the next paint, but it costs RAM. Pre-rendering the next page
in the Back/Forward Cache makes navigation instant, but the entire renderer
process must stay alive in memory. Every performance optimization in Chrome has
a memory cost that must be weighed.

```
RAM usage breakdown (rough example, one tab)
┌─────────────────────────────────────┐
│  Browser process      ~100 MB       │
│  GPU process          ~80  MB       │
│  Renderer (tab 1)     ~120 MB       │
│  Renderer (tab 2)     ~90  MB       │
│  Renderer (tab 3)     ~150 MB       │
│                       ──────        │
│  Total                ~540 MB       │
└─────────────────────────────────────┘
```

---

## C++ Memory Management in Chromium

### Raw Pointers and RAII

C++ lets you allocate objects with `new` and free them with `delete`. A **raw
pointer** (`T*`) is just an address — it carries no information about who owns
the object or when it should be freed. Forgetting to call `delete` is a memory
leak. Calling `delete` and then using the pointer is a **use-after-free** (UAF)
bug — one of the most dangerous vulnerability classes in native code.

The solution is **RAII** (Resource Acquisition Is Initialization): tie the
lifetime of a resource to the lifetime of a C++ object. When the object goes
out of scope, its destructor runs and frees the resource automatically.

```cpp
// Dangerous: who deletes this?
Widget* w = new Widget();

// Safe: deleted automatically when unique_ptr goes out of scope
std::unique_ptr<Widget> w = std::make_unique<Widget>();
```

`std::unique_ptr<T>` expresses *single ownership*. Only one unique_ptr can own
a given object; ownership can be transferred with `std::move()` but never
copied. Chromium strongly prefers `unique_ptr` for heap-allocated objects with a
clear single owner.

`std::shared_ptr<T>` uses *reference counting*: the object is deleted when the
last shared_ptr pointing to it is destroyed. Shared ownership is convenient but
can hide lifetime bugs and creates cycles.

### scoped_refptr and RefCounted

Chromium has its own reference-counting system predating `std::shared_ptr`.
Objects that need shared ownership inherit from `base::RefCounted<T>` (single
thread) or `base::RefCountedThreadSafe<T>` (multiple threads). The smart
pointer `scoped_refptr<T>` increments the count on copy and decrements on
destruction.

```cpp
class MyService : public base::RefCountedThreadSafe<MyService> { ... };

scoped_refptr<MyService> service = base::MakeRefCounted<MyService>();
// Shared across threads safely; deleted when last reference drops.
```

Use `scoped_refptr` when an object genuinely has multiple owners, especially
across threads. Prefer `unique_ptr` when ownership is singular.

### WeakPtr

Sometimes you need to *refer* to an object without *owning* it. If the object
is deleted while you still hold a reference, you have a dangling pointer. The
solution is `base::WeakPtr<T>`: a pointer that **automatically becomes null**
when the target is destroyed.

```cpp
class Downloader {
  base::WeakPtrFactory<Downloader> weak_factory_{this};
};

// In async callback:
base::WeakPtr<Downloader> weak = downloader->weak_factory_.GetWeakPtr();
task_runner->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<Downloader> d) {
  if (!d) return;  // Object was deleted; safe to bail out.
  d->OnComplete();
}, weak));
```

WeakPtr is the standard Chromium pattern for callbacks that may outlive the
object they reference.

---

## Dangling Pointers and raw_ptr\<T\>

Use-after-free is the **number one security vulnerability class** in Chrome's
history. A dangling pointer is a raw pointer that still holds the address of
memory that has already been freed. Dereferencing it is undefined behavior that
attackers can exploit to execute arbitrary code.

Chromium's solution is `raw_ptr<T>`, a drop-in replacement for `T*` in class
fields:

```cpp
// Before (dangerous):
Foo* foo_;

// After (safe):
raw_ptr<Foo> foo_;
```

`raw_ptr<T>` integrates with **PartitionAlloc** (see next section) to keep
freed memory in a **quarantine** rather than immediately reusing it. If code
tries to dereference a dangling `raw_ptr`, PartitionAlloc detects that the
memory is quarantined and crashes — in debug builds immediately, and in
production with statistical sampling.

This project is called **MiraclePtr**. It converts silent, exploitable
use-after-free bugs into loud, non-exploitable crashes.

The **DanglingPtrDetector** build flag enables crash-on-dangle during testing:

```
raw_ptr<T>                        // crashes if dangled (checked builds)
raw_ptr<T, DisableDanglingPtrDetection>  // intentionally may dangle (rare)
raw_ptr<T, DanglingUntriaged>     // pre-existing; needs investigation
```

---

## Oilpan: Blink's Garbage Collector

The DOM is a graph. A JavaScript closure can hold a reference to a DOM node,
which holds a reference back to a JS object. Manual reference counting breaks
down with cycles; garbage collection (GC) handles them naturally.

Blink uses **Oilpan**, a precise mark-and-sweep garbage collector, for DOM
objects and other objects exposed to JavaScript.

```
GC Object Hierarchy
┌──────────────────────────────────────┐
│  GarbageCollected<T>  (base class)   │
│    Member<U>          (GC pointer)   │
│    Persistent<U>      (GC root)      │
│    WeakMember<U>      (weak GC ref)  │
└──────────────────────────────────────┘
```

- **`GarbageCollected<T>`** — base class; the GC manages this object's lifetime.
- **`Member<T>`** — a pointer between two GC objects; traced during the mark phase.
- **`Persistent<T>`** — a root reference from outside the GC heap; prevents collection.
- **`CrossThreadPersistent<T>`** — like `Persistent`, but safe to use from another thread.

Oilpan uses **incremental marking**: instead of stopping the world for a full
GC cycle, it spreads marking work across multiple small pauses to avoid jank.
It coordinates with V8's garbage collector so that JavaScript objects and DOM
objects are collected together during unified GC cycles.

---

## PartitionAlloc

Most C++ programs use the system `malloc`. Chromium uses its own allocator,
**PartitionAlloc**, for two reasons:

**Security.** Objects of different types are allocated in different *partitions*
(separate memory regions). A heap buffer overflow in one partition cannot
corrupt objects in another partition, defeating many heap-spray exploit
techniques.

**Performance.** Allocating objects of similar size together improves cache
locality. PartitionAlloc's fast path is a simple bump pointer within a
pre-committed slot span, faster than a general-purpose allocator for many
patterns.

```
PartitionAlloc layout
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│  Partition A │ │  Partition B │ │  Partition C │
│  (strings)   │ │  (DOM nodes) │ │  (ArrayBuf)  │
│  [slots]     │ │  [slots]     │ │  [slots]     │
└──────────────┘ └──────────────┘ └──────────────┘
  No cross-partition pointer confusion possible
```

**PartitionAlloc-Everywhere** is the project that routes *all* heap allocations
in Chrome through PartitionAlloc, enabling MiraclePtr's quarantine mechanism
universally.

---

## Memory Per Process

Each Chrome process holds different kinds of memory:

**Browser process** — profile data, bookmarks, history, preferences, process
management tables, some cached resources.

**Renderer process (one per site/tab)** — DOM tree on the Oilpan heap, V8
JavaScript heap (JS objects, strings, closures, bytecode), layout tree, paint
artifacts, and decoded image bitmaps. This is typically the largest consumer.

**GPU process** — GPU textures for compositor layers, decoded video frames,
and shader programs. On devices where GPU memory is shared with main memory
(most mobile devices), this shows up in system RAM directly.

---

## Memory Pressure and OOM

When available RAM runs low, the operating system sends **memory pressure**
signals. Chrome listens for these and reacts in stages:

1. **Cache eviction** — discard decoded images and other soft-cached data.
2. **Back/Forward Cache eviction** — release renderer processes kept alive for
   instant back/forward navigation.
3. **Tab discarding** — freeze and evict the least-recently-used inactive tabs.
   The tab still appears in the tab strip; reloading it restores the page.

On **Android**, the OS can kill background processes at any time without
warning. Chrome structures its renderer lifecycle to survive this gracefully.

When an allocation truly fails, PartitionAlloc calls
`base::internal::OnNoMemoryInternal()`, which terminates the process with an
OOM crash rather than returning `nullptr` and risking a null-dereference
exploit.

---

## Memory Profiling Tools

| Tool | What it shows |
|------|---------------|
| `chrome://memory-internals` | Per-process memory breakdown |
| `chrome://tracing` (memory category) | Allocation timeline |
| Heap profiling component | Call-stack-attributed heap snapshots |
| `MemoryDumpProvider` interface | Per-subsystem memory reporting |

**RSS (Resident Set Size)** measures how many pages are actually in physical
RAM. Pages that have been swapped out or discarded with `madvise(MADV_FREE)`
do not count toward RSS, which is why RSS can be smaller than the virtual
address space committed by the process.

---

## Security: ASLR and Memory Safety

**Address Space Layout Randomization (ASLR)** is an OS defense that randomizes
the base addresses of the stack, heap, and loaded libraries each time a program
runs. An attacker who needs to jump to a specific address (e.g., a ROP gadget
or a shellcode buffer) cannot hardcode it. On 64-bit systems, the address space
is so large that ASLR provides billions of possible locations, making brute
force impractical.

Chrome's memory safety work layers on top of ASLR:

```
Defense layers against memory exploits
┌─────────────────────────────────────────┐
│  OS: ASLR (randomize addresses)         │
│  Chrome: Sandboxing (limit syscalls)    │
│  Chrome: PartitionAlloc (isolate heaps) │
│  Chrome: raw_ptr / MiraclePtr (UAF→crash│
│  Chrome: Oilpan GC (no manual free)     │
└─────────────────────────────────────────┘
```

A use-after-free that once meant arbitrary code execution now triggers a
deterministic crash before the attacker can control execution flow. A heap
buffer overflow in one partition cannot reach objects in another partition.
The goal is not perfection today, but continuous reduction of the attack
surface while maintaining the performance users expect.

---

## Summary

Memory management in Chrome is a multi-layered discipline. The C++ layer uses
RAII smart pointers (`unique_ptr`, `scoped_refptr`, `WeakPtr`) to express
ownership clearly. `raw_ptr<T>` and MiraclePtr harden the remaining raw
pointers against use-after-free exploitation. Blink's Oilpan garbage collector
handles the DOM's complex object graph. PartitionAlloc provides a fast,
security-conscious allocator that underpins all of these mechanisms. At the
system level, memory pressure handling and tab discarding keep Chrome usable
even on constrained devices, while ASLR and sandbox isolation limit the damage
when bugs do occur.
