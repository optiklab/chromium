# Chapter 12: Browser UI, Navigation, and Sessions

## 12.1 The Browser UI Layer

Chromium splits its code into distinct layers. `//content` is the **embedding API** — the browser engine without any visible chrome. `//chrome` is the **browser shell** that adds the address bar, tabs, menus, and all user-facing UI. `//views` is the cross-platform UI toolkit for desktop, providing widgets, layouts, and event handling that work on Windows, macOS, and Linux.

A browser window is built as a hierarchy of Views objects:

```
┌──────────────────────────────────────────────┐
│  BrowserWindow (chrome layer)                 │
│  ┌────────────────────────────────────────┐  │
│  │  Toolbar  │ Address Bar │ Tab Strip     │  │
│  └────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────┐  │
│  │  WebContents (content layer)            │  │
│  │  ┌──────────────────────────────────┐  │  │
│  │  │  Web Page (renderer process)      │  │  │
│  │  └──────────────────────────────────┘  │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

`BrowserView` (a Views widget) owns both the toolbar area and a `WebContentsView`, which is the native window surface where the renderer paints web content. This clean separation means the `//content` layer knows nothing about tabs, bookmarks, or extensions — it only knows about loading and rendering web pages.

## 12.2 WebContents: The Heart of a Tab

`WebContents` is the central class representing a single tab. It lives in the **browser process** and is the main object that `//chrome` code interacts with. It owns the `FrameTree` (the tree of frames for the primary page plus any prerendering frames), and coordinates navigation, input routing, accessibility, and audio.

```
WebContents
 ├── NavigationController  (session history)
 ├── FrameTree             (primary page frames)
 │    └── FrameTreeNode → RenderFrameHost (in renderer)
 ├── Tab Helpers           (attached features)
 └── WebContentsDelegate   (chrome-layer callbacks)
```

Because `WebContents` is so central, adding every browser feature directly to it would create an unmanageable monolithic class. That is why Chromium uses the **Tab Helpers** pattern.

## 12.3 Tab Helpers Pattern

A tab helper is a small class that attaches to a `WebContents` to implement one specific feature. It inherits from `WebContentsObserver` (to receive page lifecycle events) and `WebContentsUserData<T>` (to be stored inside the `WebContents` via a key-value map).

```cpp
class BookmarkTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<BookmarkTabHelper> {
 public:
  void DidFinishNavigation(NavigationHandle* handle) override;
  // ...
};
```

Examples include `PasswordManagerClient`, `FindTabHelper`, and `SafeBrowsingTabHelper`. Each feature team owns their helper independently. You retrieve a helper with `BookmarkTabHelper::FromWebContents(web_contents)`. This keeps `WebContents` small and features decoupled.

> **Note:** Tab Helpers are gradually being replaced by `TabFeatures`, a newer pattern described in `chrome_browser_design_principles.md`, but the underlying idea is identical.

## 12.4 Session History

Every tab has a `NavigationController` that manages its **back/forward list**. The list is an ordered array of `NavigationEntry` objects, each representing one point in history. Each entry stores the URL, page title, scroll position, form data, and a tree of `FrameNavigationEntry` objects for subframes.

```
NavigationController entries:
  [0] https://example.com/       ← oldest
  [1] https://example.com/about
  [2] https://example.com/blog   ← current (index=2)
  [3] (pruned when user navigated away after going back)
```

Key rules:
- If the user goes **back** then navigates to a new page, all **forward** entries are pruned.
- `history.pushState()` adds a new entry without a network request.
- `history.replaceState()` and `location.replace()` overwrite the current entry.
- First-party subframe navigations after the initial load create new entries so the user can navigate back within the frame.

Session history is also **persisted to disk**, allowing tabs to be restored after a crash or restart. The serialized form is a `PageState` proto. Cloning a tab copies `NavigationEntry` objects directly.

## 12.5 Back/Forward Cache (BFCache)

Normally, navigating away from a page tears down its DOM and JavaScript context. When the user presses Back, the page must be fetched and parsed again — even if it is in the HTTP cache.

**BFCache** changes this: instead of destroying the page, the browser **freezes** it in memory. On Back, the page is **thawed** and shown instantly.

```
User clicks link →  Page A frozen → Page B shown
User presses Back → Page B frozen → Page A thawed (instant!)
```

**Challenges:**
- Pages that hold exclusive resources (WebLocks, IndexedDB transactions) cannot be safely frozen.
- `unload` event handlers are not fired when a page enters BFCache, which breaks some older code.
- Privacy: a cached page must not observe that it was in the cache, nor leak state to other origins.

Pages must pass an **eligibility check** before entering BFCache. Pages using certain APIs (e.g., `Cache-Control: no-store`, open WebSockets, broadcast channels) are currently ineligible.

## 12.6 Prerendering

While BFCache speeds up backward navigation, **prerendering** speeds up forward navigation. Chrome uses the `SpeculationRules` API (a JSON block in the page's HTML) to identify URLs the user is likely to visit next, then renders them in the background.

```html
<script type="speculationrules">
  {"prerender": [{"urls": ["https://example.com/next"]}]}
</script>
```

The prerendered page lives in a **separate FrameTree** inside the same `WebContents`. It has no access to the screen or user input. When the user actually navigates to that URL, the prerendered tree is **activated** — swapped in as the primary FrameTree instantly.

## 12.7 Multiple Pages Architecture (MPArch)

To support BFCache and prerendering simultaneously, Chromium introduced **MPArch**: a single `WebContents` can now host **multiple FrameTrees** at once.

```
WebContents
 ├── Primary FrameTree        ← what the user sees
 ├── BFCache FrameTree(s)     ← frozen previous pages
 └── Prerender FrameTree(s)   ← speculative future pages
```

Code that used to assume one page per `WebContents` had to be updated. The term **OutermostMainFrame** refers to the main frame of the primary FrameTree — the one the user is currently viewing.

## 12.8 WebUI Pages

Pages like `chrome://settings`, `chrome://newtab`, and `chrome://history` are called **WebUI** pages. They look like ordinary web pages (HTML/CSS/JS) but run in a **privileged renderer** with access to browser-internal Mojo interfaces.

```
chrome://settings URL
       │
       ▼
WebUIControllerFactory
       │
       ▼
SettingsUI (WebUIController subclass)
       │  registers Mojo interfaces
       ▼
Renderer calls chrome.send() or Mojo bindings
       │
       ▼
Browser-process handler (e.g., PrefService, PasswordManager)
```

WebUI renderers are **isolated from web content** — a normal web page cannot navigate to `chrome://settings` or access its Mojo interfaces. The `chrome://` scheme is intercepted by `ChromeContentBrowserClient` and never sent over the network.

## 12.9 Extensions

Chrome Extensions extend browser functionality through a declared set of permissions in `manifest.json`. The current API version is **Manifest V3**.

- **Content scripts**: JavaScript injected into matching web pages, running in an isolated world so they cannot directly access page JS variables.
- **Background service workers**: persistent JS that runs in the extension's own process, handling events like network requests or browser actions.
- **Extension APIs**: privileged APIs (`chrome.tabs`, `chrome.storage`, `chrome.webRequest`) mediated by the browser process.

Communication uses message passing (`chrome.runtime.sendMessage`). The browser process validates every API call against the extension's declared permissions.

## 12.10 The Omnibox

The **Omnibox** (address bar) is more than a URL field — it is a combined URL entry, search box, and suggestion engine.

When the user types, the Omnibox queries multiple **autocomplete providers** in parallel:

| Provider | Source |
|---|---|
| HistoryURLProvider | URLs from browsing history |
| BookmarkProvider | Bookmarked URLs |
| SearchProvider | Search engine suggestions (via network) |
| ZeroSuggestProvider | Suggestions shown before any typing |

The Omnibox also displays **security signals**: the HTTPS padlock, dangerous site warnings, and whether the page is a known phishing site. Malformed or non-HTTPS URLs trigger visual indicators to warn users.

## 12.11 Accessibility

Chromium builds a parallel **Accessibility Tree (AX Tree)** alongside the DOM. Screen readers and other assistive technologies query this tree rather than the DOM directly.

```
DOM Tree          AX Tree (sent to OS)
<div role="button">  →  AXButton
  <span>OK</span>   →    AXStaticText "OK"
</div>
```

The AX tree is constructed in the **renderer process** and serialized to the **browser process** via an IPC channel. The browser then hands it to the OS accessibility API (`IAccessible2` on Windows, `NSAccessibility` on macOS, `ATK` on Linux). Code lives in `//ui/accessibility` (shared data structures) and `//content/browser/accessibility` (browser-side tree management).

## Summary

| Concept | Key Class | Layer |
|---|---|---|
| Tab | `WebContents` | `//content` |
| Tab feature | `TabHelper` / `TabFeatures` | `//chrome` |
| Back/forward list | `NavigationController` | `//content` |
| History entry | `NavigationEntry` | `//content` |
| Frozen page | BFCache `FrameTree` | `//content` |
| Speculative page | Prerender `FrameTree` | `//content` |
| Privileged page | `WebUIController` | `//chrome` |
| Address bar | `OmniboxView` | `//chrome` |
| UI widgets | `views::View` | `//ui/views` |

The browser UI is a layered system: `//views` provides platform-independent widgets, `//content` provides the web engine, and `//chrome` binds them together with all the features users see. Each layer has a clear responsibility, making it possible for hundreds of engineers to work on different parts simultaneously without stepping on each other.
