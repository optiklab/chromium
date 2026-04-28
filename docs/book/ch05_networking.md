# Chapter 5: The Networking Stack

> *"The network is the computer."* — Sun Microsystems motto

Every time you type a URL and press Enter, Chromium performs a remarkable
sequence of operations: it parses the address, looks up an IP, negotiates a
secure connection, checks a cache, and streams bytes from halfway around the
world — all in under a second. This chapter explains how that happens, from the
`//net` library at its core to the modern protocols that make it fast.

---

## 5.1 Architecture Overview

Chromium's networking code lives primarily in the `//net` directory, which the
[net/README.md](../../net/README.md) describes as "Chrome's networking stack."
It handles everything from DNS resolution to certificate verification to cookie
storage. The library itself is process-agnostic — it is a pure C++ library with
no process-management logic of its own.

The *Network Service*, however, is a separate OS process. This is a deliberate
security choice: if a bug in the network stack is exploited, the attacker gains
control only of the sandboxed Network Service process, not the whole browser.
Communication between the Browser Process (which holds the user's tabs and UI)
and the Network Service uses Mojo IPC (covered in Chapter 3).

```
Browser Process
  |
  | (Mojo IPC — network::mojom::NetworkService)
  v
Network Service Process
  |-- URLRequestContext          <-- the "root" object; one per profile
        |-- URLRequest           <-- represents one in-flight request
        |-- HttpNetworkSession   <-- manages connections and protocols
        |-- DiskCache            <-- persistent HTTP response cache
        |-- CookieStore          <-- cookie persistence (CookieMonster)
        |-- HostResolver         <-- DNS lookups
        |-- ProxyResolutionService  <-- PAC/WPAD proxy logic
        |-- SSLConfigService     <-- TLS settings and HSTS state
        |-- CertVerifier         <-- certificate chain validation
```

**`URLRequestContext`** is the central hub. Every network request in a given
profile shares one context, which supplies shared resources (the cache, the
cookie jar, the DNS resolver, etc.). Incognito mode simply creates a second
context whose `DiskCache` is an in-memory-only backend and whose `CookieStore`
does not persist to disk.

**`URLRequest`** is the object you create when you want to fetch a resource. You
give it a URL and a delegate (a callback interface), call `Start()`, and the
stack does the rest — DNS, TCP, TLS, HTTP, caching, redirects.

---

## 5.2 URL Parsing

Before any network activity begins, Chromium must parse the URL the user or
page provided. URLs are defined by [RFC 3986](https://tools.ietf.org/html/rfc3986)
and have the structure:

```
  scheme "://" [userinfo "@"] host [":" port] path ["?" query] ["#" fragment]
  https://user:pass@www.example.com:443/search?q=hello#results
  \___/   \_______/ \_____________/ \_/ \____/ \______/ \_____/
  scheme  userinfo      host        port  path   query  fragment
```

Chromium represents a parsed URL with the **`GURL`** class (`url/gurl.h`).
`GURL` is immutable and performs *canonicalization* at construction time:

- The hostname is lowercased (`WWW.Example.COM` → `www.example.com`)
- The path is percent-encoded consistently
- Default ports are normalized away (`http://x.com:80/` → `http://x.com/`)
- Trailing dots on hostnames are removed

Canonicalization is a **security requirement**, not just tidiness. Without it,
attackers could bypass same-origin checks by using `HTTP://EVIL.COM` vs
`http://evil.com`, or inject paths using `%2F` vs `/`.

**Internationalized Domain Names (IDN)** add another layer. Users can type
`münchen.de`, but DNS only handles ASCII. Chromium encodes non-ASCII hostnames
using Punycode (`xn--mnchen-3ya.de`). Browsers also guard against *homograph
attacks* — visually identical characters from different scripts — by displaying
the raw Punycode when a hostname mixes scripts suspiciously.

**Special schemes** get special treatment:

| Scheme     | Meaning |
|------------|---------|
| `https://` | Secure HTTP, requires TLS |
| `http://`  | Plain HTTP |
| `file://`  | Local filesystem |
| `data:`    | Inline content (`data:image/png;base64,...`) |
| `blob:`    | URL referring to an in-memory Blob object |
| `chrome://`| Browser internal pages (settings, flags, etc.) |
| `chrome-extension://` | Extension resources |

`chrome://` URLs are handled entirely within the browser process and never
reach the network stack.

---

## 5.3 DNS Resolution

DNS (Domain Name System) translates human-readable hostnames like
`www.example.com` into IP addresses like `93.184.216.34`. Without it, users
would have to memorize numbers.

Inside `//net`, resolution goes through **`net::HostResolver`**
(`net/dns/host_resolver.h`). The concrete production implementation is
`ContextHostResolver`, which is owned 1-to-1 by a `URLRequestContext`. Behind
the scenes, a single shared **`HostResolverManager`** schedules and throttles
actual DNS jobs across all contexts to avoid hammering poorly behaved home
routers with too many concurrent lookups.

```
ContextHostResolver
  |-- HostCache (in-memory, TTL-based)
  |-- HostResolverManager (shared, one per NetworkService)
        |-- DnsClient (Chrome's built-in DNS-over-UDP/TCP/HTTPS)
        |-- SystemResolver (OS getaddrinfo, as fallback)
```

**DNS caching**: Results are stored in `HostCache` with a TTL (time-to-live)
specified by the DNS record. Until TTL expires, Chromium reuses the cached IP
without querying the network. This is why a stale DNS entry can persist for
hours after a server moves.

**DNS-over-HTTPS (DoH)**: Traditional DNS sends queries in plaintext over UDP,
which allows ISPs and attackers on the path to see (or forge) every hostname you
visit. Chromium supports DoH, sending encrypted DNS queries to an HTTPS endpoint
instead. Users can configure this in `chrome://settings/security`.

**Happy Eyeballs**: Modern DNS returns both IPv4 (A records) and IPv6 (AAAA
records). Rather than trying them sequentially, Chromium uses the Happy Eyeballs
algorithm (RFC 6555): it starts both an IPv6 connection and an IPv4 connection
in parallel, with a short stagger favoring IPv6, and uses whichever succeeds
first. This eliminates long timeouts on networks where one protocol is broken.

---

## 5.4 Making a Connection

Once an IP address is known, Chromium opens a connection.

### TCP Three-Way Handshake

TCP (Transmission Control Protocol) establishes a reliable, ordered byte stream
with a three-step dance before any data flows:

```
Client                         Server
  |--- SYN ---------------------->|   "I want to connect"
  |<-- SYN-ACK ------------------|   "OK, I'm ready"
  |--- ACK ---------------------->|   "Great, let's go"
     (data can now flow)
```

This costs one round-trip before the first byte of HTTP data can be sent — a
key motivator for the connection reuse and protocol improvements described later.

### TLS/SSL Handshake

For `https://` URLs, a TLS handshake follows the TCP handshake:

1. Client sends `ClientHello` — lists supported cipher suites and TLS versions.
2. Server sends `ServerHello` + its **certificate** (a signed document from a
   Certificate Authority proving the server's identity).
3. Client verifies the certificate (see below) and they agree on a session key.
4. Encrypted traffic begins.

TLS 1.3 (the current standard) reduced this to roughly **one additional
round-trip** by pipelining more of the negotiation.

### Certificate Verification

The `CertVerifier` (`net/cert/cert_verifier.h`) validates that:

- The certificate was signed by a trusted Certificate Authority (CA) in the
  [Chrome Root Store](../data/ssl/chrome_root_store/)
- The certificate's hostname matches the server being contacted
- The certificate has not expired
- The certificate has not been revoked (checked via CRLSets — compact
  bloom-filter-like sets of revoked certificates distributed via Chromium
  component updates)
- The certificate complies with CA/Browser Forum Baseline Requirements

Even when delegating to the OS certificate verifier on Android or iOS, Chromium
applies its own additional policy checks on top.

### HSTS (HTTP Strict Transport Security)

HSTS lets a server instruct browsers: *"Always use HTTPS for this domain; never
fall back to HTTP."* Once Chromium sees an `HSTS` response header, it stores the
domain in an `SSLConfigService`-managed list. Future requests to that domain are
automatically upgraded to HTTPS before DNS is even consulted. A preloaded HSTS
list ships with Chromium to protect domains like `google.com` from the very
first connection.

### Connection Pooling

Opening a fresh TCP+TLS connection costs 2–3 round trips. Chromium maintains
a **socket pool** inside `HttpNetworkSession` that keeps established connections
alive and reuses them for subsequent requests to the same host, amortizing that
startup cost over many requests.

---

## 5.5 HTTP/1.1, HTTP/2, and HTTP/3

### HTTP/1.1

The classic version. Each request occupies an entire connection until its
response is fully received. Loading a page with 30 resources meant browsers
opened 6 parallel connections per host just to achieve concurrency — wasteful
and still suffering from *head-of-line blocking* where a slow response blocks
later ones.

### HTTP/2 (built on SPDY)

HTTP/2 grew directly from SPDY, a protocol Google developed and shipped in
Chrome around 2009–2012. The `net/spdy/` directory contains Chromium's HTTP/2
implementation. Key improvements over HTTP/1.1:

- **Multiplexing**: multiple requests share one TCP connection simultaneously,
  identified by stream IDs. No more per-host connection sprawl.
- **HPACK header compression**: HTTP headers repeat on every request (cookies,
  User-Agent, etc.). HPACK uses a shared compression table to send headers
  as short deltas, cutting header overhead dramatically.
- **Server Push**: the server can proactively send resources the client will
  need before it asks (though this feature is rarely used in practice).

HTTP/2 eliminates application-level head-of-line blocking, but TCP itself still
has it: one lost packet stalls the entire connection at the transport layer.

### HTTP/3 (built on QUIC)

HTTP/3 solves the TCP head-of-line problem by running over **QUIC**, a
Chromium-originated protocol that runs on UDP. QUIC provides:

- **Independent stream delivery**: a lost packet only stalls the stream whose
  data it carries, not every stream on the connection.
- **0-RTT connection establishment**: returning clients can send data in the
  very first packet (no TCP + TLS round trips).
- **Built-in encryption**: QUIC always encrypts; there is no plaintext mode.
- **Connection migration**: a QUIC connection identified by a connection ID (not
  by IP:port) can survive moving between Wi-Fi and cellular without reconnecting.

Chrome's QUIC implementation lives largely in the
[QUICHE library](https://quiche.googlesource.com/quiche), with Chrome-specific
glue in `net/quic/`.

### Protocol Negotiation (ALPN)

How does the browser know which protocol the server supports? During the TLS
handshake, both sides advertise supported application protocols using the **ALPN
extension** (Application-Layer Protocol Negotiation). The server picks the best
mutually supported option (`h2` for HTTP/2, `h3` for HTTP/3). For QUIC/HTTP/3,
the server advertises support via an `Alt-Svc` response header on an HTTP/1.1 or
HTTP/2 connection, and Chromium upgrades on the next visit.

---

## 5.6 Caching

Fetching the same resource twice wastes bandwidth and time. Chromium has a
two-level cache.

### Disk Cache

The `net/disk_cache/` directory provides a persistent HTTP response cache.
It stores response headers and bodies keyed by URL (plus a few other factors).
Two main backends exist:

- **Blockfile** (Windows): packs many small entries into "block" files for
  performance, at the cost of more complex recovery from corruption.
- **Simple** (Linux, macOS, Android, ChromeOS): roughly one file per cache entry,
  with an in-memory index for fast membership tests. More robust and portable.

Incognito mode uses the in-memory backend from `disk_cache/memory/`, so nothing
persists to disk.

Each cache entry stores up to three data streams: stream 0 for HTTP headers,
stream 1 for the response body, and stream 2 for auxiliary data like compiled
JavaScript bytecode.

### Cache Validation

Cached responses come with freshness information. The `Cache-Control: max-age=N`
directive says "this response is fresh for N seconds." Before that time, the
browser serves the cached copy without contacting the server — a *cache hit*.

After expiry, the browser sends a *conditional request*:
- `If-None-Match: "etag-value"` — server returns `304 Not Modified` if unchanged
- `If-Modified-Since: <date>` — same idea, using timestamps

The **`stale-while-revalidate`** pattern is an interesting middle ground: the
cache serves the stale response immediately (fast!) while simultaneously
revalidating in the background so the *next* request gets a fresh copy.

### Cache Partitioning (Privacy)

Before 2020, Chromium used a single shared cache across all sites. This allowed
*cache probing attacks*: a malicious site could detect whether you had visited a
target site by timing whether a shared resource loaded from cache or from
network. Chromium now **partitions the cache** by the top-level site and the
frame's site, so `cdn.example.com/lib.js` loaded from `evil.com` and from
`news.com` occupy separate cache entries. This costs some cache efficiency but
closes a meaningful privacy leak.

---

## 5.7 Cookies

Cookies are small key-value pairs that servers store in the browser to identify
returning users and maintain session state. They are defined by
[RFC 6265bis](https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis) and
managed in Chromium by **`CookieMonster`**, the main implementation of
`CookieStore`.

### Life of a Cookie

1. Server sends `Set-Cookie: session=abc123; Secure; HttpOnly; SameSite=Lax`
2. `URLRequestHttpJob` parses the header into a `CanonicalCookie`
3. `CookieMonster` stores it in memory and, for persistent cookies, to a SQLite
   database on disk
4. On future requests to the same domain, `CookieMonster` serializes the
   matching cookies into a `Cookie: session=abc123` request header

### Key Cookie Attributes

| Attribute | Meaning |
|-----------|---------|
| `Secure` | Only send over HTTPS, never HTTP |
| `HttpOnly` | JavaScript cannot read this cookie (`document.cookie` returns nothing) |
| `SameSite=Lax` | Don't send on cross-site subresource requests, only on top-level navigations |
| `SameSite=Strict` | Never send on any cross-site request |
| `Max-Age` | Expiry in seconds; without it, cookie is session-only |

### Partitioned Cookies (CHIPS)

The **Cookies Having Independent Partitioned State** proposal (CHIPS) allows
third-party cookies when they are explicitly partitioned by the top-level site.
A `Partitioned` cookie set by `widget.com` while embedded in `shop.com` is only
sent when `widget.com` is again embedded in `shop.com` — not on every other site
that also embeds `widget.com`. This preserves useful cross-site functionality
while eliminating cross-site tracking.

### Third-Party Cookie Deprecation

Cookies set by domains other than the top-level site have historically been the
primary mechanism for cross-site tracking. Chromium is phasing out support for
third-party cookies without the `Partitioned` attribute, replacing tracking use
cases with privacy-preserving APIs like the Privacy Sandbox proposals.

---

## 5.8 WebSockets

WebSockets provide full-duplex (simultaneous two-way) communication between a
browser and a server over a persistent connection — essential for chat
applications, live sports scores, and collaborative editing.

### The Upgrade Handshake

A WebSocket connection starts as a regular HTTP/1.1 request:

```
GET /chat HTTP/1.1
Host: example.com
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

If the server agrees, it responds with `101 Switching Protocols`, and from that
moment the TCP connection carries WebSocket frames instead of HTTP. The
`Sec-WebSocket-Key` / `Sec-WebSocket-Accept` handshake prevents plain HTTP
servers from accidentally being treated as WebSocket endpoints.

Chromium's implementation lives in `net/websockets/` and supports WebSockets
over both HTTP/1.1 and HTTP/2 (the latter multiplexes WebSocket streams as
regular HTTP/2 streams). The network service enforces per-renderer throttling
on WebSocket connections to prevent runaway pages from exhausting system
resources.

---

## 5.9 Security in the Network Stack

### Certificate Transparency

Certificate Transparency (CT) is a public, append-only log of every TLS
certificate ever issued. Chromium enforces that certificates from publicly
trusted CAs must appear in at least two independent CT logs before it trusts
them. This makes it much harder for a rogue CA to issue a fraudulent certificate
undetected, since any mis-issuance would be visible to the public.

### Mixed Content Blocking

An `https://` page that loads resources over `http://` is *mixed content*. Those
insecure sub-resources can be intercepted and modified by an attacker, even
though the main page is secure. Chromium blocks mixed content (especially
*active* mixed content like scripts) and auto-upgrades passive mixed content
(images, audio) to HTTPS where possible.

### CORS and CORB

**CORS (Cross-Origin Resource Sharing)** is a browser security policy
preventing a page at `evil.com` from reading data from `bank.com` using
`fetch()`. When a cross-origin request is made, the browser checks whether the
server's response includes `Access-Control-Allow-Origin` headers permitting the
requesting origin. Without them, the response is blocked before JavaScript can
see it.

**CORB (Cross-Origin Read Blocking)** adds a second layer: even if CORS would
allow the *request*, Chromium blocks the *response* from entering a renderer
process if the response type (e.g., HTML, JSON) is incompatible with the context
it was loaded into (e.g., as an `<img>` source). This protects sensitive data
even against bugs in renderer code.

### Network Traffic Annotations

Every call to the network stack in Chromium is required to carry a
`net::NetworkTrafficAnnotationTag` — a compile-time-checked documentation tag
that records: what component triggers the request, what user data is sent, what
the destination is, and what user settings can disable it. This creates an
auditable, in-code inventory of all network communication, helping privacy
reviewers ensure no "back doors" exist. Tools can mechanically verify that no
unannotated network request is possible.

---

## 5.10 Proxy Support

Many corporate networks and privacy tools route traffic through proxy servers.
Chromium supports two configuration modes.

### Manual Proxy

A user or administrator specifies a proxy server address directly
(`proxy.corp.example.com:8080`). All requests (or requests matching specified
URL patterns) are sent to the proxy, which forwards them to the internet.

### PAC Files and WPAD

**Proxy Auto-Config (PAC)** files are small JavaScript programs that, given a
URL and host, return either `"DIRECT"` (connect directly) or
`"PROXY proxy.example.com:8080"`. This allows fine-grained rules: route
internal hostnames directly, everything else through the proxy.

```javascript
// Example PAC function
function FindProxyForURL(url, host) {
    if (isInNet(host, "10.0.0.0", "255.0.0.0")) {
        return "DIRECT";          // internal traffic: no proxy
    }
    return "PROXY proxy.corp.com:8080";  // everything else: via proxy
}
```

**WPAD (Web Proxy Auto-Discovery)** automates PAC file discovery: Chromium
queries DNS for the hostname `wpad.<local-domain>` to find the PAC file URL
automatically without manual configuration. To avoid startup delays when no
WPAD server exists, Chromium uses an aggressive 100ms DNS timeout for WPAD
discovery (versus the normal DNS timeout for explicitly configured PAC URLs).

The `ProxyResolutionService` inside `URLRequestContext` manages all proxy logic
— caching the PAC script result per URL pattern, re-fetching the PAC file
periodically, and falling back to `DIRECT` on proxy failure when configured.

---

## Summary

The Chromium networking stack is a complete, self-contained HTTP client library
that runs inside its own sandboxed process. Starting from a URL string, it:

1. **Parses** the URL with `GURL` (canonicalization, IDN encoding)
2. **Resolves DNS** with `HostResolver` (caching, DoH, Happy Eyeballs)
3. **Negotiates a proxy** via PAC/WPAD if configured
4. **Opens a TCP connection** (or reuses a pooled one)
5. **Performs a TLS handshake** and verifies the certificate chain
6. **Negotiates the protocol** (HTTP/1.1, HTTP/2, or HTTP/3 via ALPN)
7. **Checks the cache** before sending a request
8. **Sends and receives** HTTP frames, updating the cache and cookies
9. **Enforces security policies** (HSTS, mixed content, CORS, CORB)

Each step has been refined over years to be faster, more secure, and more
private than its predecessor. The progression from HTTP/1.1 to HTTP/2 to HTTP/3
reflects a continuous effort to push web performance to its limits while
tightening security with every iteration.

---

*Next: Chapter 6 — The Renderer Process and Blink*
