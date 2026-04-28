# Chapter 4: Life of a URL – The Browser Pipeline

> *"When you press Enter, a thousand things happen before a single pixel
> appears on screen. This chapter traces every one of them."*

---

## The Big Picture

A web browser's single most important job is turning a URL into a page. What
looks like one action from the outside — press Enter, see a page — is actually a
carefully orchestrated pipeline involving the browser process, one or more
renderer processes, the operating system's network stack, DNS servers, and the
remote web server. Understanding this pipeline is the foundation for
understanding almost everything else in Chromium.

Here is the complete pipeline, which we will walk through step by step:

```
User types URL and presses Enter
            ↓
  Omnibox processes input
  (URL vs. search query, URL parsing)
            ↓
  BeforeUnload check on current page
  (does the page want to prevent you leaving?)
            ↓
  NavigationRequest created
  (browser process begins orchestrating)
            ↓
  Network request
  DNS resolution → TCP connection → TLS handshake → HTTP request
            ↓
  Response received
  HTTP status code checked (2xx / 3xx / 4xx / 5xx)
  MIME type detected
            ↓
  Process selection
  (which renderer process gets the new document?)
            ↓
  Navigation Commit
  (renderer acknowledges; browser updates security state & history)
            ↓
  Document parsed  (HTML → DOM)
            ↓
  Subresources loaded  (CSS, JS, images, fonts)
            ↓
  Rendering pipeline  (Layout → Paint → Composite)
            ↓
  Page displayed to user 🎉
```

Keep this diagram in mind. Every section below is one box in this pipeline.

---

## Step 1: URL Input and the Omnibox

Everything starts when the user types in the **Omnibox** — Chromium's combined
address-bar-and-search-box. The Omnibox does more than accept text; it
*interprets* it.

### What is a valid URL?

A well-formed URL has several parts:

```
https://www.example.com:443/path/to/page?query=value#section
  ↑           ↑           ↑      ↑           ↑          ↑
scheme       host        port   path        query     fragment
```

- **Scheme** (`https`, `http`, `ftp`, `chrome`, `data`, `blob`, …) tells
  the browser *how* to fetch the resource.
- **Host** is the domain name (or IP address) of the server.
- **Port** is optional; browsers use well-known defaults (80 for HTTP, 443
  for HTTPS).
- **Path** identifies the resource on the server.
- **Query** passes parameters to the server (`?key=value&key2=value2`).
- **Fragment** (`#section`) refers to a position within the already-loaded
  document — it is *never sent to the server*.

### URL vs. search query

The Omnibox performs **disambiguation**: is the user entering a URL or a search
query? If the text doesn't look like a URL (no scheme, no recognisable TLD, has
spaces, etc.), the Omnibox constructs a search-engine URL instead. This
classification happens in the browser process, in Chromium's AutocompleteInput
code.

### Navigation initiated in the browser process

Once the Omnibox decides on a final URL, the navigation is
**browser-initiated** — it originates in the browser process rather than in a
renderer process. Browser-initiated navigations are considered more trustworthy
because they come from the browser's own UI, not from potentially hostile web
content. This distinction matters later for security decisions such as whether
to show the pending URL in the address bar.

---

## Step 2: BeforeUnload

Before making any network request, Chromium must check whether the *current*
page objects to being left.

### The `beforeunload` event

A web page can register a `beforeunload` event handler:

```js
window.addEventListener('beforeunload', (event) => {
    event.preventDefault();          // trigger confirmation dialog
    event.returnValue = '';          // required for cross-browser compat
});
```

When a navigation is about to start, Chromium sends a message to the current
renderer process asking it to run any registered `beforeunload` handlers. If the
handler calls `event.preventDefault()`, the browser displays a confirmation
dialog asking the user whether they really want to leave. If the user clicks
*Cancel*, the navigation is **completely aborted** and no further work is done.
If the user clicks *Leave* (or there was no handler), the pipeline continues.

The `DidStartNavigation` observer callback fires *after* the `beforeunload`
check passes, just before the network request begins.

---

## Step 3: The Network Request

With the user's permission to leave the current page confirmed, Chromium creates
a **NavigationRequest** object in the browser process. This object is the
central bookkeeping record for the entire navigation — it tracks the URL,
initiator, response headers, chosen renderer process, and dozens of other
details.

> **Note:** Not every navigation actually hits the network. Service Workers can
> intercept requests, WebUI pages are served from the browser process itself,
> and pages restored from the Back/Forward Cache (BFCache) skip the network
> entirely. The description below covers the common case of a real HTTP(S)
> request.

### DNS resolution

The first network step is translating the human-readable hostname
(`www.example.com`) into an IP address. The browser asks the operating system's
DNS resolver, which queries a DNS server (usually your router or ISP). The
result — an IP address like `93.184.216.34` — is cached to speed up future
requests.

### TCP connection

With an IP address in hand, Chromium opens a **TCP connection** to port 443 (for
HTTPS). TCP is the reliable, ordered transport layer; it guarantees that bytes
arrive in order and without corruption.

### TLS handshake

For HTTPS, the TCP connection is immediately upgraded with a **TLS handshake**.
The browser and server negotiate a cipher suite, exchange certificates, and
establish an encrypted channel. This is what the padlock icon in the address bar
represents.

### HTTP request and response

The browser sends an HTTP request:

```
GET /path/to/page HTTP/1.1
Host: www.example.com
Accept: text/html,application/xhtml+xml,...
...
```

The server responds with a status code and headers, followed by the response
body. The status code determines what happens next:

| Code range | Meaning | What Chromium does |
|---|---|---|
| **2xx** | Success | Proceed to process the response |
| **3xx** | Redirect | Follow the `Location` header; issue a new request |
| **4xx** | Client error (e.g. 404 Not Found) | Commit an error page |
| **5xx** | Server error | Commit an error page |

**Redirects (3xx):** Chromium follows redirects automatically, issuing a new
request to the URL in the `Location` response header. Each redirect fires
`DidRedirectNavigation` on `WebContentsObserver`, and Navigation Throttles get
a `WillRedirectRequest` callback to inspect or block the redirect.

**Two special non-rendering cases:**
- **204/205 (No Content):** The server signals success but provides no new
  document. The current page stays visible; the navigation ends without
  committing.
- **`Content-Disposition: attachment`:** The server is sending a file to be
  saved, not displayed. Chromium starts a download instead of a navigation.

### MIME type detection

If the response doesn't include a `Content-Type` header (and the server hasn't
sent `X-Content-Type-Options: nosniff`), Chromium performs **MIME type
sniffing**: it reads the first few bytes of the response body and guesses the
content type. This ensures that, for example, an HTML file served without a
`Content-Type` header is still correctly interpreted as HTML.

*(Chapter 5 covers the network stack in depth.)*

---

## Step 4: Process Selection

Once a successful response is in hand, the browser process must decide: **which
renderer process** should handle this new document?

This is a critical security decision. Chromium's **Site Isolation** policy
requires that pages from different origins (e.g., `bank.example.com` vs.
`attacker.example.com`) be placed in separate renderer processes, so that a
compromised renderer cannot read another origin's data.

The unit Chromium uses is the **SiteInstance** — a group of documents that share
the same *site* (scheme + registrable domain) and can therefore be in the same
process. The browser process consults the current process model and isolation
policy (including **Cross-Origin-Opener-Policy** headers, which can force a
document into a new browsing context group) to decide whether to:

1. **Reuse** the current renderer process (same-site navigation).
2. **Reuse** a different existing renderer process (same-site as another tab).
3. **Spawn a new** renderer process (cross-site navigation or isolation policy
   requirement).

The `ReadyToCommitNavigation` observer method fires once the browser process has
chosen a renderer and is about to send it the response, but before it actually
does so — this is the last opportunity for C++ code to inspect or modify the
about-to-be-committed navigation.

---

## Step 5: Navigation Commit

Committing a navigation is the moment the new document officially *replaces* the
old one. It involves a round-trip between the browser process and the renderer
process:

1. The browser process sends the response (headers + initial body data) to the
   chosen renderer process.
2. The renderer begins creating the new document object.
3. The renderer sends back an **acknowledgement** ("I have started the new
   document").
4. Upon receiving this ack, the browser process:
   - Updates its **security state** (origin, HTTPS status, etc.) to reflect the
     new document.
   - Creates a new **NavigationEntry** (session history item) for the previous
     document, so the user can press Back to return to it.
   - Updates the URL shown in the address bar to the committed URL.

### Unloading the old document

What happens to the *old* page during commit depends on whether the navigation
is same-process or cross-process:

- **Same-process navigation:** Blink (the rendering engine) synchronously
  unloads the old document — running any `unload` event handlers — before
  creating the new document.
- **Cross-process navigation:** The new document is created in the new renderer
  process *concurrently* with running `unload` handlers in the old process.
  This parallelism is an optimisation that speeds up cross-site navigations.

`DidFinishNavigation` fires after the commit, passing a `NavigationHandle` that
callers can query to determine whether the navigation succeeded or resulted in
an error page.

---

## Step 6: Document Loading

Navigation and loading are **two distinct phases** in Chromium. Navigation ends
at commit; loading is everything that follows.

### Why the split?

Errors are handled differently before and after commit. A 404 response commits
an *error page* — the user sees Chromium's "not found" page. But if an error
happens *after* a successful commit (e.g., the connection drops mid-stream),
Chromium shows as much of the real page as it received, rather than replacing it
with an error page. The commit is the point of no return.

### Parsing and subresource loading

After commit, the renderer process:

1. **Parses HTML** into the **Document Object Model (DOM)** — a tree of objects
   representing every element on the page. *(Covered in Chapter 7.)*
2. As the parser encounters `<script>`, `<link>`, `<img>`, and other tags, it
   issues **subresource requests** for JavaScript files, CSS stylesheets, images,
   and fonts. These requests go through Chromium's network stack just like the
   main navigation, but they are ordinary resource loads, not navigations.
3. JavaScript found in `<script>` tags is executed by the **V8 engine**. Scripts
   can modify the DOM, trigger more network requests, and generally do anything.

### DOMContentLoaded vs. load

Two events mark important milestones:

- **`DOMContentLoaded`**: fires when the HTML has been fully parsed and the DOM
  is ready, even if images and stylesheets haven't finished loading yet. This is
  the earliest point at which JavaScript can safely query the full DOM.
- **`load`**: fires when the document *and all its subresources* (images, CSS,
  etc.) have finished loading.

The corresponding `WebContentsObserver` callbacks are `DOMContentLoaded` and
`DidFinishLoad`.

---

## Step 7: Rendering

With the DOM constructed and subresources available, the renderer produces
pixels. The rendering pipeline has three major stages:

1. **Layout**: Calculates the size and position of every element on the page,
   implementing CSS rules. This produces a **layout tree**.
2. **Paint**: Walks the layout tree and records drawing operations ("draw this
   text in blue at position (100, 50)") into a **display list**.
3. **Composite**: Breaks the page into **layers** (e.g., scrolling content gets
   its own layer), rasterises each layer into pixels, and assembles them. On
   most platforms this final assembly happens on the GPU.

### Web performance metrics

Two widely used measurements of perceived load speed are defined in terms of the
rendering pipeline:

- **First Contentful Paint (FCP)**: the moment any text, image, or other
  non-blank content first appears on screen.
- **Largest Contentful Paint (LCP)**: the moment the *largest* image or text
  block becomes visible. LCP is a core **Core Web Vitals** metric.

*(Chapter 10 covers the rendering pipeline in detail.)*

---

## Navigation Throttles

Chromium's **NavigationThrottle** API lets C++ code intercept navigations at
key points in the pipeline, without having to modify the navigation core itself.
Throttles are registered per-navigation and receive callbacks at three main
points:

| Callback | When it fires |
|---|---|
| `WillStartRequest` | Before the initial network request is sent |
| `WillRedirectRequest` | Each time a 3xx redirect is about to be followed |
| `WillProcessResponse` | After the response headers arrive, before commit |

At each callback, a throttle can return one of:
- **PROCEED** — do nothing, let the navigation continue.
- **DEFER** — pause the navigation (asynchronous work needed).
- **CANCEL** — abort the navigation entirely.
- **CANCEL_AND_IGNORE** — abort and leave the current page unchanged.

Throttles are used for many features: Safe Browsing URL checks, enterprise
content policies, signed exchange handling, and more. They apply only to
navigations that involve a URL loader (real network requests). Same-document
navigations and `about:blank` use a separate
`WillCommitWithoutUrlLoader` callback instead.

> **Important:** Page-activation navigations — restoring a page from BFCache or
> activating a prerendered page — **skip NavigationThrottles entirely**, because
> no new network request is made.

---

## Special Cases

The pipeline described above covers the common case. Several types of navigations
deviate from it.

### Same-document navigations

A **same-document navigation** does not create a new document. There is no
network request, no commit in the cross-process sense, and no `beforeunload`
check. It arises from:

- **Fragment navigation** (`https://example.com/page#section`): the browser
  scrolls to the element with the matching `id`, and adds a new history entry.
- **`history.pushState()` / `history.replaceState()`**: JavaScript updates the
  URL in the address bar and (optionally) the history stack without loading a
  new document. Single-Page Applications (SPAs) rely heavily on this mechanism.

### Back/Forward navigation and BFCache

The **Back/Forward Cache (BFCache)** is an optimisation where Chromium keeps
recently-visited pages fully alive in memory (frozen) rather than discarding
them. When the user presses Back or Forward, the frozen page is *unfrozen* and
displayed immediately — no network request, no re-parse, no re-render.
Page-activation navigations like BFCache restores skip NavigationThrottles and
do not go through the normal commit flow.

### Service Workers

A **Service Worker** is a JavaScript file that runs in the background and can
intercept network requests made by a page. When a navigation request is made to
a URL controlled by a Service Worker, the worker's `fetch` event fires and can
return a custom response from cache — completely bypassing the network.

### WebUI pages (`chrome://`)

Pages like `chrome://settings` and `chrome://flags` are implemented directly
inside Chromium, not served from the internet. They use the `chrome:` (or `os:`)
scheme. The browser routes these navigations to built-in handlers in the browser
process rather than making a network request. Web pages cannot navigate to
`chrome:` URLs — doing so is blocked to prevent privilege escalation.

### `data:` and `blob:` URLs

- **`data:` URLs** embed the resource content directly in the URL itself
  (e.g., `data:text/html,<h1>Hello</h1>`). They bypass the network entirely.
- **`blob:` URLs** refer to in-memory binary data created by JavaScript (e.g.,
  the result of `URL.createObjectURL()`). They are also served locally, without
  a network request.

---

## WebContentsObserver: Observing the Pipeline

Chromium exposes the navigation lifecycle through the `WebContentsObserver`
interface. Any C++ class that wants to react to navigations inherits from it and
overrides the relevant methods.

### Navigation callbacks (in order)

```
DidStartLoading          – spinner appears; navigation is about to start
DidStartNavigation       – after beforeunload passes; before network request
DidRedirectNavigation    – called for each 3xx redirect encountered
ReadyToCommitNavigation  – renderer chosen; last chance to inspect before commit
DidFinishNavigation      – navigation committed (success or error page)
```

### Loading callbacks (in order, after commit)

```
DOMContentLoaded    – HTML parsed; DOM is ready
DidFinishLoad       – document + all subresources loaded
DidStopLoading      – top-level document, all subframes, all subresources done
DidFailLoad         – load failed (e.g. network dropped mid-stream)
```

`DidStartLoading` and `DidStopLoading` fire once per `WebContents` (the
browser-process object that represents a tab), while `DOMContentLoaded`,
`DidFinishLoad`, and `DidFailLoad` fire per `RenderFrameHost` (one per frame,
including iframes).

---

## Putting It All Together

Let's trace a single, concrete example: the user types
`https://www.example.com/` and presses Enter.

1. **Omnibox** recognises it as a URL (has scheme, has host), leaves it
   unchanged.
2. **BeforeUnload** — previous page has no handler, continues immediately.
3. **NavigationRequest** created; `DidStartNavigation` fires.
4. **DNS** resolves `www.example.com` → `93.184.216.34`.
5. **TCP** connects to port 443.
6. **TLS** handshake; encrypted channel established.
7. **HTTP GET** request sent; server returns `200 OK` with
   `Content-Type: text/html`.
8. **No redirect**, no `Content-Disposition`, MIME type confirmed as HTML.
9. **Process selection**: cross-site from the previous page, so a new renderer
   process is created.
10. **ReadyToCommitNavigation** fires; throttles are satisfied.
11. **Commit**: response sent to new renderer, renderer ACKs. Browser updates
    security state, creates history entry. `DidFinishNavigation` fires.
12. **Parsing**: renderer parses HTML into a DOM.
13. **Subresources**: `<link rel="stylesheet">`, `<script>`, and `<img>` tags
    trigger additional network requests.
14. `DOMContentLoaded` fires when HTML parsing is complete.
15. `DidFinishLoad` fires when images and CSS finish.
16. **Rendering**: layout, paint, composite → pixels sent to GPU.
17. **FCP** and eventually **LCP** are recorded.
18. `DidStopLoading` fires. The spinner stops.

The whole pipeline — from Enter key to final pixel — typically completes in
under a second on a fast connection, yet involves coordination between multiple
OS processes, the DNS infrastructure, a remote server, and thousands of lines of
carefully designed C++ code.

---

## Summary

| Stage | Key object | Key observer callback |
|---|---|---|
| URL parsing | AutocompleteInput | — |
| BeforeUnload | RenderFrameHost | DidStartNavigation |
| Network request | NavigationRequest | DidStartNavigation, DidRedirectNavigation |
| Process selection | SiteInstance | ReadyToCommitNavigation |
| Commit | NavigationController | DidFinishNavigation |
| DOM parsing | Document (Blink) | DOMContentLoaded |
| Subresource loading | ResourceFetcher | DidFinishLoad |
| Rendering | LayerTreeHost | (FCP / LCP metrics) |

The pipeline is not strictly linear — many steps overlap (subresource loads are
parallel, compositing runs on a separate thread) — but the logical ordering
described here matches how the browser's state machine progresses. The following
chapters dive into the network layer (Chapter 5), security model (Chapter 6),
HTML parsing and the DOM (Chapter 7), and the rendering pipeline (Chapter 10).

---

*Next: [Chapter 5 – The Network Stack](ch05_network_stack.md)*
