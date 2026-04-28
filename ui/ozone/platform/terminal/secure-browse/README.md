# Edge Secure Browse — Disposable Browser Isolation

Run a fully isolated, disposable web browser over SSH. The browser renders
as text in your terminal. No pixels, no images, no binary data leaves the
server — only ANSI text flows through the SSH connection.

## Threat Model

```
┌─────────────────┐    SSH (text only)    ┌─────────────────────────┐
│  Your Machine    │◄════════════════════►│  Hardened Server         │
│                  │  encrypted stream     │                         │
│  Terminal        │  of characters        │  Edge terminal browser  │
│  (display only)  │                      │  ┌───────────────────┐  │
│                  │  No pixels            │  │ Temporary profile │  │
│  • No browser    │  No cookies           │  │ (wiped on exit)   │  │
│  • No web code   │  No DOM               │  └───────────────────┘  │
│  • No JS engine  │  No executables       │                         │
│  • No risk       │  No files             │  All web code runs HERE │
│                  │                       │  All risk stays HERE    │
└─────────────────┘                       └─────────────────────────┘
```

### What stays on the server

- All JavaScript execution
- All DOM content and web APIs
- All cookies, session tokens, passwords
- All downloaded files
- All browser profile data
- All network traffic to websites
- GPU/rendering engine attack surface

### What reaches your machine

- ANSI escape sequences (colored text characters)
- That's it

### What happens on exit

- The temporary profile directory is `rm -rf`'d
- No cookies, history, cache, or credentials survive
- Each session starts completely clean

## Security Features

| Feature | Status | Description |
|---------|--------|-------------|
| Temporary profile | ✅ Always | Fresh `/tmp/` dir, wiped on exit |
| Extensions disabled | ✅ Always | `--disable-extensions` |
| Sync disabled | ✅ Always | `--disable-sync` |
| Autofill disabled | ✅ Always | No form data saved |
| Password saving disabled | ✅ Always | No credentials stored |
| Crash reporting disabled | ✅ Always | No data sent to crash servers |
| Metrics disabled | ✅ Always | `--metrics-recording-only` |
| Background networking disabled | ✅ Always | Minimal phone-home |
| DNS-over-HTTPS | ✅ Default | Cloudflare DoH |
| First-run disabled | ✅ Always | No setup wizard |
| tmux isolation | ✅ Always | Session survives SSH disconnect |

## Quick Start

### On the server (one-time setup)

```bash
# Build Edge with terminal platform support
autoninja -C out/linux_x64_debug_developer_build chrome

# Make the script executable
chmod +x ui/ozone/platform/terminal/secure-browse/secure-browse.sh
```

### From your local machine

```bash
# SSH in and browse — everything is wiped when you disconnect
ssh -t server "cd /path/to/edge/src && ./ui/ozone/platform/terminal/secure-browse/secure-browse.sh https://example.com"
```

### Interactive usage on the server

```bash
# Basic usage — opens new tab page
./secure-browse.sh

# Browse a specific URL
./secure-browse.sh https://suspicious-site.com

# Named session you can reconnect to
./secure-browse.sh --session investigation --keep https://target-site.com

# Higher resolution with sixel
./secure-browse.sh --render-mode sixel https://example.com
```

## Usage

```
secure-browse.sh [OPTIONS] [URL]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--session NAME` | random | tmux session name |
| `--keep` | off | Don't wipe profile on exit |
| `--cell-size WxH` | `4x8` | Terminal cell pixel ratio |
| `--render-mode M` | `halfblock` | `halfblock` or `sixel` |
| `--edge-bin PATH` | `$EDGE_BIN` | Path to msedge binary |
| `-h, --help` | — | Show help |

## Use Cases

### 1. Investigating suspicious URLs

```bash
# Analyst workstation — no risk to your machine
ssh -t sandbox-server "secure-browse https://phishing-site.example.com"
# Browse the suspicious site, take notes
# Disconnect — all data destroyed
```

### 2. Disposable login sessions

```bash
# Log into a sensitive service from a clean environment
ssh -t clean-server "secure-browse https://admin-panel.internal"
# Session tokens never touch your laptop
# Close terminal — credentials gone
```

### 3. Shared investigation (multiple analysts)

```bash
# Analyst 1: Start a named session
ssh server "secure-browse --session case-42 --keep https://target.com"

# Analyst 2: Attach to the same session
ssh -t server "tmux attach -t case-42"
# Both analysts see and control the same browser in real-time
```

### 4. Air-gapped-style browsing

```bash
# The SSH connection carries only text
# Even if the server is compromised, the attacker gets:
#   - ANSI text stream (not exploitable on modern terminals)
#   - Keyboard input (limited to key/mouse events)
# They do NOT get:
#   - Access to your filesystem
#   - Access to your clipboard
#   - Access to your display server
#   - Ability to run code on your machine
```

### 5. Automated security scanning

```bash
# Combine with the LLM agent for automated analysis
python3 ui/ozone/platform/terminal/agent/terminal_agent.py \
    --goal "Navigate to example.com and list all external links, forms, and scripts" \
    --verbose
```

## Session Management

```bash
# List active sessions
tmux list-sessions

# Reattach to a session
tmux attach -t edge-secure-abc123

# Detach from inside: Ctrl-B D

# Kill a session manually
tmux kill-session -t edge-secure-abc123
```

## Hardening the Server

For maximum isolation, run the browser server in a container or VM:

```bash
# Docker example (concept)
docker run --rm -it \
    --network=bridge \
    --read-only \
    --tmpfs /tmp \
    --cap-drop=ALL \
    edge-terminal-image \
    secure-browse https://example.com
```

Recommended server hardening:
- Minimal OS (no GUI, no display server)
- No X11/Wayland forwarding
- Dedicated non-root user for browsing
- Network egress filtering (allow only HTTP/HTTPS/DNS)
- Resource limits (CPU, memory, disk)
- Ephemeral VM/container per session (ultimate isolation)

## Comparison with Existing Browser Isolation

| Feature | Traditional RBI | Edge Terminal |
|---------|----------------|---------------|
| Data format over network | Pixels (video stream) | Text (ANSI) |
| Bandwidth | 2-10 Mbps | ~50 Kbps |
| Client software | Proprietary viewer | Any SSH client |
| Works over SSH | No | Yes |
| Works in tmux | No | Yes |
| Latency sensitivity | High (video) | Low (text) |
| Client attack surface | Video decoder | Terminal emulator |
| Resolution | Full HD | Low (text) / medium (sixel) |
| Cost | $$$ cloud service | Free (self-hosted) |

## Limitations

- **Low visual fidelity** — text-mode rendering means images appear as
  colored blocks. Sufficient for text-heavy sites, forms, and
  investigations. Use sixel mode for better resolution.

- **No file download** — files download to the server, not your machine.
  Use `scp` to retrieve if needed (intentional security boundary).

- **No clipboard sharing** — copy/paste between local and remote requires
  tmux buffer or OSC 52 clipboard support in your terminal.

- **JavaScript still runs** — on the server. This is browser isolation,
  not content disarm. The JS can't reach your machine, but it runs on
  the server. Container isolation is recommended.

## Related

- [Edge Terminal Platform](../) — the Ozone platform implementation
- [LLM Web Agent](../agent/) — AI-driven browsing automation
