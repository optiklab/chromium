# Chapter 9: Security Architecture

## Introduction

A web browser is one of the most security-critical pieces of software on your
computer. Every day it downloads and executes code from thousands of untrusted
sources—random websites written by strangers, some of whom may be actively
malicious. Chromium's security architecture is a carefully layered system of
defenses designed to ensure that even when something goes wrong (and in a
codebase this large, something occasionally does), the blast radius is as small
as possible. This chapter walks through those layers from the outermost to the
innermost.

---

## 9.1 The Browser as a Security Boundary

Why do attackers care about browsers? Because they run with the same privileges
as the user who launched them. If an attacker can trick your browser into
executing arbitrary code, they can read your files, steal your passwords, and
install malware—all without ever convincing you to run an `.exe`. The browser is
one of the most attractive attack surfaces on any desktop system.

The fundamental attacker goal is **privilege escalation**: starting from
JavaScript running in a tab (very restricted) and working upward toward code
running as the browser process (full user-level access) or, ideally for the
attacker, as the OS kernel.

Chromium counters this with **defense in depth**: multiple independent security
layers, so that compromising any single one does not hand the attacker a win.

```
┌─────────────────────────────────────────────────────┐
│  Operating System                                    │
│  ┌───────────────────────────────────────────────┐  │
│  │  Browser Process (trusted, high privilege)     │  │
│  │  ┌─────────────────────────────────────────┐  │  │
│  │  │  Renderer Process (sandboxed, low priv)  │  │  │
│  │  │  ┌───────────────────────────────────┐  │  │  │
│  │  │  │  Web Content / JavaScript          │  │  │  │
│  │  │  │  (untrusted, origin-isolated)      │  │  │  │
│  │  │  └───────────────────────────────────┘  │  │  │
│  │  └─────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘

  Each boundary is an independent line of defense.
  An attacker must breach ALL of them to fully compromise the system.
```

---

## 9.2 The Same-Origin Policy (SOP)

The **Same-Origin Policy** is the cornerstone of web security. It governs which
JavaScript code can read data from which sources.

An **origin** is the combination of three things:

```
origin = scheme + host + port

https://example.com:443   ← one origin
https://example.com:8080  ← different origin (different port)
http://example.com:80     ← different origin (different scheme)
https://sub.example.com   ← different origin (different host)
```

The rule is simple: **JavaScript running in one origin cannot read data from a
different origin.** A page on `attacker.com` cannot use JavaScript to read your
emails on `gmail.com`, even if both are open in tabs in the same browser.

What SOP *does* allow:
- Loading images, scripts, CSS, and iframes from other origins (but not *reading* their contents)
- Sending cross-origin POST requests (with some restrictions)
- Receiving data that the server explicitly opts into sharing via **CORS** (Cross-Origin Resource Sharing)

In Blink, origins are represented by the `SecurityOrigin` class. Every document
and worker has one, and it is checked constantly—when accessing `localStorage`,
`postMessage`, `XMLHttpRequest`, cookies, and more.

---

## 9.3 The Sandbox

Even with SOP, a bug in the HTML parser or JavaScript engine could allow an
attacker to execute arbitrary native code inside the renderer process. The
**sandbox** is what stops that native code from doing real damage.

The sandbox drastically reduces what a renderer process is *allowed to do at the
OS level*, regardless of what code is running inside it. From Chromium's
sandbox design document:

> "The sandbox objective is to provide hard guarantees about what ultimately a
> piece of code can or cannot do no matter what its inputs are."

**What the sandbox blocks:**
- Direct file system access (reading or writing arbitrary files)
- Registry access on Windows
- Direct network connections (all networking is proxied through the browser process)
- Most OS system calls
- Creation of new processes
- Access to clipboard, global hooks, and system-wide UI changes

The sandbox is OS-specific:

**Windows:** Uses four mechanisms together: a *restricted token* (strips nearly
all privileges and group memberships, runs at "Untrusted" integrity level), a
*Job object* (prevents spawning child processes, blocks clipboard access and
global hooks), a *Desktop object* (isolates the window station), and modern
*process mitigations* (Control Flow Guard, ACG, etc.).

**Linux:** Uses `seccomp-bpf`, a kernel feature that allows a process to install
a filter program specifying exactly which system calls it is permitted to make.
All others are denied. Renderers are also run under a restrictive set of Linux
namespaces.

**macOS:** Uses Apple's Seatbelt framework (`sandbox(7)`). A profile written in
a Scheme-like language describes exactly which file paths and OS resources the
process may access.

The sandbox follows the **Principle of Least Privilege**: every process gets the
minimum set of capabilities it needs to function, and no more.

---

## 9.4 Site Isolation and Compromised Renderers

The sandbox limits what a compromised renderer can do at the OS level, but what
about data *inside* the browser? Could a compromised renderer steal data
belonging to a different website?

This is precisely the threat that **Site Isolation** addresses.

### The Threat Model

Chromium's threat model explicitly assumes that a renderer process *can* be
fully compromised—that an attacker has found a memory corruption bug and is
executing arbitrary native code inside the renderer's sandbox. The question
then becomes: what damage can they do?

Without Site Isolation, multiple origins might share a single renderer process.
A compromised renderer for `attacker.com` could read memory belonging to your
`bank.com` tab in the same process. Site Isolation prevents this by guaranteeing
that each site (scheme + eTLD+1, e.g., `example.com`) runs in its own dedicated
renderer process.

### Cross-Origin Read Blocking (CORB / ORB)

Even with process isolation, a compromised renderer might try to *fetch* data
from another origin and read it. **Cross-Origin Read Blocking (CORB)**, now
evolving into **Opaque Response Blocking (ORB)**, stops this at the network
layer: sensitive cross-site responses (HTML, JSON, XML) are blocked *before*
they ever reach the renderer process, so the compromised renderer never gets
a chance to read them.

### IPC Validation: `CanAccessDataForOrigin`

A compromised renderer can forge any IPC message it wants. It might send an IPC
claiming to be `bank.com` and ask the browser process to hand over cookies or
localStorage for that origin.

The browser process defends against this with **origin lock checks**. When a
renderer process is created, it is locked to a specific site. Every sensitive
IPC from a renderer is validated against this lock using functions like
`CanAccessDataForOrigin()`. If a renderer process locked to `attacker.com`
claims to be `bank.com` in an IPC, the browser process kills it.

```
Renderer (locked to attacker.com)          Browser Process
      │                                         │
      │  "Give me cookies for bank.com"  ──────►│
      │                                         │ CanAccessDataForOrigin(
      │                                         │   process_lock=attacker.com,
      │                                         │   requested=bank.com) → FALSE
      │◄──── [PROCESS KILLED] ──────────────────│
```

Similarly, security-sensitive values like the committed origin of a frame are
*never* taken from IPC messages. The browser process maintains its own
trustworthy record via `RenderFrameHost::GetLastCommittedOrigin()`.

---

## 9.5 The Rule of 2

Chrome's security team has codified a key principle for writing safe code that
handles untrusted data. It involves three "dangerous" properties:

1. **Untrustworthy inputs** — data from the internet (web pages, files, network packets)
2. **Unsafe implementation language** — C or C++, which can have memory corruption bugs
3. **High privilege** — running in the browser process or GPU process, where a bug has large impact

**The Rule of 2**: any given piece of code may combine *at most two* of these
three. Using all three simultaneously is not acceptable.

```
                    ┌─────────────────────┐
                    │  Untrustworthy      │
                    │  Inputs             │
                    └────────┬────────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌──────────┐  ┌──────────┐  ┌──────────────┐
        │Unsafe    │  │ ALL 3 =  │  │ High         │
        │Language  │  │ FORBIDDEN│  │ Privilege    │
        └──────────┘  └──────────┘  └──────────────┘

  Any two of the three is OK. All three is a critical security risk.
```

**In practice:** if you are parsing untrusted network data in C++, you must run
in a sandboxed process (eliminating "high privilege"). This is why PDF
rendering, image decoding, audio/video decoding, and font rendering all happen
in dedicated, sandboxed utility processes. These tasks involve complex C++ parsers
consuming untrusted data—they satisfy the Rule of 2 only because they run at low
privilege inside the sandbox.

---

## 9.6 HTTPS and Certificate Security

**HTTPS** (HTTP over TLS) provides two critical security properties:

- **Confidentiality:** traffic between your browser and the server is encrypted,
  so an attacker on the network ("man in the middle") cannot read it.
- **Authentication:** the server presents a certificate proving its identity,
  so you know you're actually talking to `bank.com` and not an impostor.

Chromium validates certificates against a set of trusted **Certificate
Authorities (CAs)**. A certificate is only trusted if it chains up to a CA in
the root store and has not expired or been revoked.

**Certificate Transparency (CT)** is an additional safeguard: all publicly
trusted certificates must be logged in a public, append-only ledger. This makes
it nearly impossible for a rogue CA to issue a fraudulent certificate without
it being detected.

**HSTS (HTTP Strict Transport Security)** allows servers to declare that they
should *only* be contacted over HTTPS, ever. Once a browser sees an HSTS header,
it will refuse plain HTTP connections to that site for the declared duration—
even if a user types `http://` in the address bar.

**Mixed content blocking** prevents HTTPS pages from loading insecure HTTP
resources (images, scripts, iframes), which would undermine the security
guarantee of the HTTPS connection.

---

## 9.7 Content Security Policy (CSP)

**Content Security Policy** is an HTTP response header that lets a website
declare exactly what content is allowed to load on its pages. It is a powerful
defense against **Cross-Site Scripting (XSS)** attacks (see Section 9.8).

```
Content-Security-Policy: default-src 'self';
                         script-src 'self' https://cdn.example.com;
                         img-src *;
                         connect-src 'self' https://api.example.com;
```

Key directives:
- `script-src`: which origins may supply JavaScript
- `img-src`: which origins may supply images
- `connect-src`: which origins `fetch()` and XHR may contact
- `default-src`: fallback for unspecified directives

The `'strict-dynamic'` keyword is a modern improvement: rather than whitelisting
specific domains (which can be bypassed), it trusts scripts that were explicitly
loaded by a trusted, nonce-verified script, allowing CSP to work well with
dynamically loaded code.

Chromium enforces CSP in the renderer process at the point where resources are
loaded and scripts are executed.

---

## 9.8 Cross-Site Scripting (XSS) Defenses

**Cross-Site Scripting (XSS)** is a class of attack where an attacker manages
to inject malicious JavaScript into a page on a victim website. Because the
script runs under the victim site's origin, it can read cookies, access
`localStorage`, and make API calls as that user.

Example: if `news.example.com` displays user comments without sanitizing them,
an attacker might post a comment containing `<script>stealCookies()</script>`.
Every user who views that comment runs the attacker's script.

**Trusted Types** is Chromium's modern approach to preventing XSS. It is a
browser API that forces developers to use audited, sanitizing wrapper functions
before passing strings into dangerous DOM sinks like `innerHTML` or
`document.write`. Code that tries to assign a raw string to `innerHTML` is
blocked if a Trusted Types policy is active.

This is an improvement over the old **XSS Auditor** (which has been removed),
which tried to detect and block reflected XSS payloads by pattern-matching—an
approach that proved both bypassable and over-aggressive.

**CSP** (Section 9.7) provides a complementary defense: a strict CSP with
`script-src 'nonce-...'` prevents injected `<script>` tags from executing even
if they get into the page.

---

## 9.9 Cross-Site Request Forgery (CSRF) Defenses

**CSRF** exploits the fact that browsers automatically attach cookies to every
request to a given origin, even if that request was initiated by a *different*
origin. An attacker on `evil.com` can embed a form that silently POSTs to
`bank.com/transfer`, and the browser will helpfully attach the user's `bank.com`
session cookie.

Modern defenses:

**Same-Site cookies:** The `SameSite=Strict` or `SameSite=Lax` cookie attribute
tells the browser to *not* attach a cookie to cross-site requests. A `Strict`
cookie is never sent cross-site; a `Lax` cookie is sent only on top-level
navigations (e.g., clicking a link).

**CSRF tokens:** Server-side technique where every sensitive form includes a
secret, per-session token. The server validates this token on each request.
Since `evil.com` cannot read the victim site's HTML (SOP prevents it), it
cannot include a valid token.

**`Sec-Fetch-Site` header:** Chromium automatically adds this header to all
requests, telling the server whether the request is `same-origin`, `same-site`,
`cross-site`, or `none` (user navigation). Servers can use this to reject
unexpected cross-site requests without fragile token logic.

---

## 9.10 Side-Channel Attacks: Spectre

In 2018, the **Spectre** hardware vulnerability revealed that a program can
read arbitrary memory it doesn't own by exploiting *speculative execution* in
modern CPUs and measuring timing differences. A malicious JavaScript program
on `attacker.com` could potentially read memory belonging to the `bank.com`
tab in the same browser process.

This fundamentally changed the security model of the web. The response had two
prongs:

**1. Process Isolation:** Since Spectre can only read memory within the same
process address space, the most effective defense is to ensure sensitive data
from different origins never shares a process. Site Isolation (Section 9.4) is
thus also a Spectre mitigation.

**2. Cross-Origin Isolation headers:** A set of HTTP headers that together
create "cross-origin isolation"—a stronger guarantee required before powerful
APIs like `SharedArrayBuffer` (which can be used as a Spectre timer) are
enabled:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

- **COOP (Cross-Origin-Opener-Policy):** Prevents a cross-origin page from
  getting a reference to your window (via `window.open()` or `target="_blank"`),
  ensuring your page is in its own browsing context group and thus its own
  process.
- **COEP (Cross-Origin-Embedder-Policy):** Requires that all subresources loaded
  by a page either come from the same origin or include a
  `Cross-Origin-Resource-Policy` header opting in to being embedded.
- **CORP (Cross-Origin-Resource-Policy):** An HTTP header that servers attach to
  their responses to explicitly control whether those responses may be loaded
  by cross-origin pages.

Only pages that set both COOP and COEP—achieving "cross-origin isolation"—are
allowed to use `SharedArrayBuffer` and high-resolution timers.

---

## 9.11 IPC Security

As described in Chapter 3, all communication between the sandboxed renderer and
the privileged browser process goes through Mojo IPC. This IPC channel is a
critical security boundary.

**The golden rule:** every IPC message arriving in the browser process from a
renderer should be treated as if it were sent by a malicious adversary.

From Chromium's Mojo security guide:

> "When receiving data from a less trusted process, treat the data as if it were
> generated by a malicious adversary. Message handlers cannot assume that offsets
> are valid, calculations won't overflow, et cetera."

**What this means in practice:**

- The browser process must validate *all* parameters from renderers—array
  indices, sizes, URLs, and especially origin claims.
- The browser process should not rely on the renderer to tell it which origin
  a frame is at; it should use its own authoritative record
  (`RenderFrameHost::GetLastCommittedOrigin()`).
- File paths sent by renderers must be defanged before use—a compromised
  renderer might send `~/.bashrc` as a download destination.
- Privilege flows *downward*: browser → renderer messages can be trusted by the
  renderer, but renderer → browser messages are always suspect.

```
  renderer (untrusted)          browser process (trusted)
       │                                │
       │  OpenLocalStorage(origin=      │
       │    "https://bank.com")  ──────►│
       │                                │ Check: does process lock
       │                                │ match "bank.com"?
       │                                │ → NO → kill renderer
```

New IPC interfaces in Chromium are reviewed by the security team to ensure they
do not introduce new attack surfaces. Guidelines are documented in the
`//docs/security/mojo.md` style guide.

---

## 9.12 Chrome's Vulnerability Rewards Program

Despite all these defenses, bugs happen. Chrome runs a **Vulnerability Rewards
Program (VRP)** to incentivize security researchers to report vulnerabilities
responsibly rather than sell or exploit them.

Researchers who find and report qualifying security bugs are eligible for cash
rewards. Bugs are classified by severity:

| Severity | Examples | Reward range |
|----------|----------|-------------|
| **Critical** | Remote code execution in browser process, full sandbox escape | Highest |
| **High** | Sandbox escape, SOP bypass, UXSS | High |
| **Medium** | Information leak across origins, partial SOP bypass | Medium |
| **Low** | Minor information leaks, minor UI spoofing | Lower |

Once a bug is reported, it is kept private until a fix is shipped to a majority
of users. After approximately 14 weeks, fixed bugs are made public. This
**responsible disclosure** window balances transparency (so the security
community can learn) with user safety (so exploits are not public before users
are patched).

Security bugs are hidden from the public issue tracker at first—even the
existence of a bug can give attackers a hint. Only after the fix ships widely
are the full details opened.

---

## 9.13 Putting It All Together: Defense in Depth

No single security mechanism is perfect. Chromium's strength comes from layering
many independent defenses:

```
Threat: Attacker delivers malicious web page
            │
            ▼
    ┌───────────────────┐
    │  HTTPS / CT       │  ← Is this the real server?
    └────────┬──────────┘
             │
    ┌────────▼──────────┐
    │  Same-Origin      │  ← Can this script touch other origins?
    │  Policy           │
    └────────┬──────────┘
             │
    ┌────────▼──────────┐
    │  CSP / Trusted    │  ← Can injected code even run?
    │  Types            │
    └────────┬──────────┘
             │
    ┌────────▼──────────┐
    │  Sandbox          │  ← Even if renderer is owned, can it
    │                   │    touch the filesystem or OS?
    └────────┬──────────┘
             │
    ┌────────▼──────────┐
    │  Site Isolation + │  ← Can a compromised renderer touch
    │  IPC Validation   │    other sites' data in the browser?
    └────────┬──────────┘
             │
    ┌────────▼──────────┐
    │  Rule of 2 /      │  ← Was the vulnerable component
    │  Sandboxed        │    even allowed to be in this config?
    │  Parsers          │
    └───────────────────┘
```

An attacker trying to go from a malicious web page to OS-level code execution
must pierce every layer. The more layers, the higher the cost and complexity of
a full attack chain—and the more opportunity Chromium has to detect and stop it.

---

## Summary

| Mechanism | What it protects against |
|-----------|--------------------------|
| Same-Origin Policy | Script-level cross-origin data theft |
| HTTPS + CT + HSTS | Network interception, identity spoofing |
| Sandbox | OS-level exploitation from a compromised renderer |
| Site Isolation | Cross-origin memory reads via Spectre or compromised renderers |
| CORB / ORB | Cross-origin data exfiltration via fetch |
| IPC validation | Compromised renderer lying about its origin |
| Rule of 2 | High-severity bugs from parsing in high-privilege contexts |
| CSP / Trusted Types | XSS and script injection |
| Same-Site cookies | CSRF attacks |
| COOP / COEP / CORP | Spectre timing attacks via shared memory |
| VRP | Encouraging responsible disclosure of remaining bugs |

Security in Chromium is not one feature—it is a philosophy of layered, paranoid,
defense-in-depth engineering applied consistently across the entire codebase.

---

*Next: Chapter 10 — Extensions and the Extension System*
