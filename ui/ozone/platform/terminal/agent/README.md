# Edge Terminal Browser — LLM Web Agent

An AI agent that controls a real web browser through text. The browser renders
its UI as characters in a terminal. The agent reads the screen, decides what
to do, and injects keyboard/mouse input — all through plain text.

## How It Works

```
┌─────────────────────────────────────────────────────┐
│  Python Agent Script                                 │
│                                                      │
│  1. tmux capture-pane   →  capture screen as text    │
│  2. Strip ANSI codes    →  clean character grid      │
│  3. Send to LLM API    →  "what do you see?          │
│                            what action next?"         │
│  4. Parse JSON response →  {action, params}          │
│  5. tmux send-keys      →  inject input              │
│  6. Wait for re-render  →  goto 1                    │
│                                                      │
└──────────┬──────────────────────────┬────────────────┘
           │ capture                  │ inject
           ▼                          ▼
┌─────────────────────────────────────────────────────┐
│  tmux session                                        │
│  ┌─────────────────────────────────────────────────┐ │
│  │  msedge --ozone-platform=terminal               │ │
│  │  (full Chromium browser rendering as ANSI text)  │ │
│  └─────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

### What the LLM sees

The browser UI is captured as a grid of characters. The LLM receives something
like this (with row/column numbers for spatial awareness):

```
         1234567890123456789012345678901234567890...
  1 | ←  →  ↻   [ https://www.bing.com              ] ☆  ⋯
  2 |
  3 |                     Microsoft Bing
  4 |
  5 |          [________________________Search___]
  6 |
  7 |    News    Images    Videos    Maps    More
  8 |
```

The LLM can read button labels, URL bar content, link text, form fields —
all as plain text. No vision model needed.

### What the LLM responds

The LLM outputs a single JSON action per turn:

```json
{"action": "click", "col": 35, "row": 5}
```

The agent translates this into an SGR mouse event injected into the tmux
session. The browser processes it like a real click.

## Prerequisites

- **Linux** (the terminal Ozone platform only works on Linux)
- **tmux** installed (`apt install tmux`)
- **Python 3.10+**
- **Edge built with terminal platform** (`ozone_platform_terminal = true` in args.gn)
- **OpenAI-compatible API** (OpenAI, Azure OpenAI, Ollama, etc.)

No Python dependencies beyond the standard library.

## Quick Start

```bash
# 1. Build Edge with terminal platform support (on Linux)
autoninja -C out/linux_x64_debug_developer_build chrome

# 2. Set your API key
export OPENAI_API_KEY="sk-..."

# 3. Run the agent with a goal
python3 ui/ozone/platform/terminal/agent/terminal_agent.py \
    --goal "Go to bing.com and search for 'Edge browser release notes'"
```

## Usage

```
python3 terminal_agent.py --goal "TASK DESCRIPTION" [OPTIONS]
```

### Required arguments

| Argument | Description |
|----------|-------------|
| `--goal` | What the agent should accomplish |

### Optional arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--api-base` | `https://api.openai.com/v1` | OpenAI-compatible API endpoint |
| `--api-key` | `$OPENAI_API_KEY` | API key |
| `--model` | `gpt-4` | Model name |
| `--url` | none | Starting URL to navigate to |
| `--browser-cmd` | *(built-in)* | Override the browser launch command |
| `--width` | `120` | Terminal width in columns |
| `--height` | `40` | Terminal height in rows |
| `--max-steps` | `30` | Maximum agent steps before stopping |
| `--render-delay` | `2.0` | Seconds to wait after each action |
| `--verbose` | off | Print screen captures and LLM responses |

### Environment variables

| Variable | Purpose |
|----------|---------|
| `OPENAI_API_KEY` | API key (alternative to `--api-key`) |
| `OPENAI_API_BASE` | API base URL (alternative to `--api-base`) |
| `OPENAI_MODEL` | Model name (alternative to `--model`) |

## Examples

### Search the web

```bash
python3 terminal_agent.py \
    --goal "Search Bing for 'weather in Seattle' and tell me the temperature" \
    --url "https://www.bing.com"
```

### Navigate browser settings

```bash
python3 terminal_agent.py \
    --goal "Open Edge settings and find the default search engine" \
    --url "edge://settings"
```

### Fill a form

```bash
python3 terminal_agent.py \
    --goal "Go to example.com/contact, fill name as 'Test User' and email as 'test@example.com', then submit"
```

### Security audit

```bash
python3 terminal_agent.py \
    --goal "Navigate to example.com and list all external links on the page" \
    --verbose
```

### With Azure OpenAI

```bash
python3 terminal_agent.py \
    --goal "Search for Edge release notes" \
    --api-base "https://YOUR_RESOURCE.openai.azure.com/openai/deployments/YOUR_DEPLOYMENT/v1" \
    --api-key "$AZURE_OPENAI_KEY" \
    --model "gpt-4"
```

### With local Ollama

```bash
python3 terminal_agent.py \
    --goal "Search for Edge release notes" \
    --api-base "http://localhost:11434/v1" \
    --api-key "ollama" \
    --model "llama3"
```

## Available Actions

The LLM can issue these actions:

| Action | Parameters | Description |
|--------|-----------|-------------|
| `click` | `col`, `row` | Click at terminal cell position |
| `type` | `text` | Type literal text (sent as keystrokes) |
| `key` | `name` | Press a special key (Enter, Tab, Escape, etc.) |
| `scroll` | `direction` | Scroll up or down |
| `wait` | `seconds` | Wait for page to load (max 10s) |
| `done` | `result` | Goal completed — describe outcome |
| `stuck` | `reason` | Cannot make progress — explain why |

## Architecture Details

### Why this works

The terminal Ozone platform renders the entire browser UI (tabs, omnibox,
buttons, web content) as ANSI-colored text characters. This means:

1. **The screen IS text** — no OCR or vision model needed
2. **Input IS text** — keyboard and mouse events are escape sequences
3. **tmux provides the bridge** — capture screen text, inject input text
4. **Any LLM works** — the task is pure text comprehension + reasoning

### Data flow

```
Browser pixels (SkBitmap)
    → EdgeTerminalRenderer converts to ANSI text
    → Written to stdout (terminal PTY)
    → tmux captures as text
    → Agent strips ANSI codes
    → Sent to LLM as plain text grid
    → LLM reasons about layout and content
    → LLM outputs JSON action
    → Agent converts to tmux send-keys / SGR escape sequence
    → Injected into tmux PTY
    → EdgeTerminalInputHandler reads from stdin
    → Browser processes as UI event
    → Browser re-renders → loop
```

### Mouse click encoding

The browser uses SGR xterm mouse tracking. Clicks are sent as escape
sequences through tmux:

```
Press:   \033[<0;col;rowM
Release: \033[<0;col;rowm
```

Where `col` and `row` are 1-based terminal cell coordinates.

### Conversation management

The agent maintains a conversation history with the LLM. To prevent context
overflow, old screen captures are trimmed after 20 messages, keeping only the
system prompt, initial goal, and the last 8 exchanges.

## Limitations

- **Low resolution text** — in half-block mode, the browser renders at
  ~480×320 pixels for a 120×40 terminal. Text is readable but coarse.
  Use `--terminal-cell-size=2x4` for larger UI elements, or
  `--terminal-render-mode=sixel` if your terminal supports it.

- **Screen capture lag** — there's a delay between action and re-render.
  The `--render-delay` flag controls how long the agent waits.

- **No image understanding** — the LLM sees text characters, not images.
  Photos, icons, and non-text content appear as colored blocks.

- **Coordinate precision** — the LLM must count characters to click
  accurately. Row/column numbers in the prompt help, but small targets
  can be missed.

## Related

- [Edge Terminal Ozone Platform](../) — the browser-side implementation
- [Sixel mode](../) — higher resolution rendering with `--terminal-render-mode=sixel`
