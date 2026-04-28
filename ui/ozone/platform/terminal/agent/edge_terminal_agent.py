#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""LLM-driven web agent that controls Edge browser rendered in a terminal.

The browser runs inside a tmux session using the terminal Ozone platform.
The agent captures the screen as text, sends it to an LLM, and executes
the LLM's chosen action (click, type, key press, scroll) by injecting
input back into the tmux session.

Usage:
    export OPENAI_API_KEY="your-key"
    python3 terminal_agent.py --goal "Search for weather in Seattle"

Works with any OpenAI-compatible API (Azure OpenAI, GPT-4, Ollama, etc.)
by setting --api-base and --model.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

TMUX_SESSION = "edge-agent"

# Default browser command. Override with --browser-cmd.
DEFAULT_BROWSER_CMD = (
    "./out/linux_x64_debug_developer_build/msedge"
    " --ozone-platform=terminal"
    " --enable-features=msTerminalPlatform"
    " --terminal-cell-size=4x8"
    " --user-data-dir=/tmp/edge-agent-profile"
    " --no-first-run"
    " --disable-sync"
)

SYSTEM_PROMPT = """\
You are an AI agent controlling a web browser rendered as text in a terminal.
The screen is a grid of characters: columns (left-right) and rows (top-down).
Column 1 is the leftmost, row 1 is the topmost.

You can see UI elements like the URL bar, tab titles, buttons, links, and
page content as text characters. Interpret the layout spatially.

Respond with EXACTLY ONE action as a JSON object on a single line:

  {"action": "click", "col": N, "row": N}
    Click at terminal cell (col, row). Use for buttons, links, URL bar.

  {"action": "type", "text": "..."}
    Type text. The text will be sent as keystrokes.

  {"action": "key", "name": "Enter"}
    Press a special key. Valid: Enter, Tab, Escape, Backspace, Up, Down,
    Left, Right, Space, PageUp, PageDown, Home, End, Delete.

  {"action": "scroll", "direction": "down"}
    Scroll the page. Valid directions: up, down.

  {"action": "wait", "seconds": N}
    Wait for the page to load. Use after navigation.

  {"action": "done", "result": "..."}
    The goal is complete. Describe what was accomplished.

  {"action": "stuck", "reason": "..."}
    You cannot make progress. Explain why.

Rules:
- Issue ONE action per turn. You will see the updated screen after each action.
- To navigate, click the URL bar, type the URL, press Enter.
- To search, click the search box, type your query, press Enter.
- After clicking or navigating, wait for the page to render.
- If the screen looks unchanged after an action, try a different approach.
- Be precise with click coordinates — count characters carefully.
"""


def strip_ansi(text: str) -> str:
    """Remove ANSI escape sequences from text."""
    return re.sub(r"\033\[[0-9;]*[A-Za-z]|\033\].*?\033\\|\033P.*?\033\\", "", text)


def tmux_is_running() -> bool:
    """Check if our tmux session exists."""
    result = subprocess.run(
        ["tmux", "has-session", "-t", TMUX_SESSION],
        capture_output=True,
    )
    return result.returncode == 0


def tmux_start_browser(browser_cmd: str, width: int, height: int) -> None:
    """Start the browser in a new tmux session with fixed dimensions."""
    # Kill existing session if any.
    subprocess.run(
        ["tmux", "kill-session", "-t", TMUX_SESSION],
        capture_output=True,
    )

    # Create detached session with specified size.
    subprocess.run(
        [
            "tmux", "new-session",
            "-d",
            "-s", TMUX_SESSION,
            "-x", str(width),
            "-y", str(height),
            browser_cmd,
        ],
        check=True,
    )
    print(f"Started browser in tmux session '{TMUX_SESSION}' ({width}x{height})")


def tmux_capture(width: int, height: int) -> str:
    """Capture the current tmux pane content as plain text."""
    result = subprocess.run(
        [
            "tmux", "capture-pane",
            "-t", TMUX_SESSION,
            "-p",           # print to stdout
            "-e",           # include escape sequences (for ANSI)
        ],
        capture_output=True,
        text=True,
    )
    raw = result.stdout
    # Strip ANSI codes to get clean text grid.
    clean = strip_ansi(raw)
    return clean


def tmux_send_keys(*keys: str) -> None:
    """Send keys to the tmux session."""
    subprocess.run(
        ["tmux", "send-keys", "-t", TMUX_SESSION] + list(keys),
        check=True,
    )


def tmux_send_literal(text: str) -> None:
    """Send literal text to the tmux session."""
    subprocess.run(
        ["tmux", "send-keys", "-t", TMUX_SESSION, "-l", text],
        check=True,
    )


def tmux_send_mouse_click(col: int, row: int) -> None:
    """Send an SGR mouse click (press + release) to the tmux session.

    The terminal browser's input handler expects SGR mouse sequences:
    \\033[<0;col;rowM  (press)
    \\033[<0;col;rowm  (release)
    """
    # tmux send-keys with Escape sends \\033.
    press_seq = f"[<0;{col};{row}M"
    release_seq = f"[<0;{col};{row}m"
    tmux_send_keys("Escape", press_seq, "Escape", release_seq)


def tmux_send_scroll(direction: str) -> None:
    """Send a scroll wheel event via SGR mouse protocol.

    Button 64 = scroll up, button 65 = scroll down.
    We click at the center of the screen.
    """
    btn = 64 if direction == "up" else 65
    # Send at center of screen.
    seq = f"[<{btn};60;20M"
    tmux_send_keys("Escape", seq)


SPECIAL_KEYS = {
    "enter": "Enter",
    "tab": "Tab",
    "escape": "Escape",
    "backspace": "BSpace",
    "up": "Up",
    "down": "Down",
    "left": "Left",
    "right": "Right",
    "space": "Space",
    "pageup": "PageUp",
    "pagedown": "PageDown",
    "home": "Home",
    "end": "End",
    "delete": "DC",
}


def execute_action(action: dict) -> str:
    """Execute an action and return a status message."""
    act = action.get("action", "")

    if act == "click":
        col = action.get("col", 1)
        row = action.get("row", 1)
        tmux_send_mouse_click(col, row)
        return f"Clicked at ({col}, {row})"

    elif act == "type":
        text = action.get("text", "")
        tmux_send_literal(text)
        return f"Typed: {text!r}"

    elif act == "key":
        name = action.get("name", "Enter").lower()
        tmux_key = SPECIAL_KEYS.get(name, name)
        tmux_send_keys(tmux_key)
        return f"Pressed: {name}"

    elif act == "scroll":
        direction = action.get("direction", "down")
        tmux_send_scroll(direction)
        return f"Scrolled {direction}"

    elif act == "wait":
        seconds = min(action.get("seconds", 2), 10)
        time.sleep(seconds)
        return f"Waited {seconds}s"

    elif act == "done":
        return f"DONE: {action.get('result', '')}"

    elif act == "stuck":
        return f"STUCK: {action.get('reason', '')}"

    else:
        return f"Unknown action: {act}"


def call_llm(
    messages: list[dict],
    api_base: str,
    api_key: str,
    model: str,
) -> str:
    """Call an OpenAI-compatible chat completion API."""
    import urllib.request
    import urllib.error

    url = f"{api_base.rstrip('/')}/chat/completions"
    headers = {
        "Content-Type": "application/json",
    }
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    body = json.dumps({
        "model": model,
        "messages": messages,
        "temperature": 0.2,
        "max_tokens": 256,
    }).encode("utf-8")

    req = urllib.request.Request(url, data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data["choices"][0]["message"]["content"].strip()
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8", errors="replace")
        print(f"LLM API error {e.code}: {error_body}", file=sys.stderr)
        raise
    except Exception as e:
        print(f"LLM API error: {e}", file=sys.stderr)
        raise


def parse_action(response: str) -> dict | None:
    """Extract JSON action from LLM response."""
    # Try to find JSON object in the response.
    # The LLM might include explanation text around it.
    for line in response.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue

    # Fallback: try to find JSON anywhere.
    match = re.search(r"\{[^{}]+\}", response)
    if match:
        try:
            return json.loads(match.group())
        except json.JSONDecodeError:
            pass

    return None


def run_agent(
    goal: str,
    api_base: str,
    api_key: str,
    model: str,
    browser_cmd: str,
    width: int,
    height: int,
    max_steps: int,
    render_delay: float,
    start_url: str | None,
    verbose: bool,
) -> None:
    """Main agent loop."""
    # Start browser.
    full_cmd = browser_cmd
    if start_url:
        full_cmd += f" {start_url}"
    tmux_start_browser(full_cmd, width, height)

    # Wait for browser to start rendering.
    print("Waiting for browser to start...")
    time.sleep(5)

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": f"GOAL: {goal}\n\nI will show you the browser screen. Decide what action to take."},
    ]

    for step in range(1, max_steps + 1):
        print(f"\n{'='*60}")
        print(f"Step {step}/{max_steps}")
        print(f"{'='*60}")

        # Capture screen.
        screen = tmux_capture(width, height)

        if verbose:
            print("--- Screen ---")
            print(screen)
            print("--- End Screen ---")

        # Add screen to conversation.
        # Add column/row ruler for LLM spatial awareness.
        ruler = "         " + "".join(
            [str((i + 1) % 10) for i in range(width)]
        )
        numbered_lines = []
        for i, line in enumerate(screen.splitlines()):
            row_num = f"{i + 1:3d} | "
            numbered_lines.append(row_num + line)

        annotated_screen = (
            f"Screen ({width} cols x {height} rows):\n"
            f"      {ruler}\n"
            + "\n".join(numbered_lines)
        )

        messages.append({
            "role": "user",
            "content": annotated_screen,
        })

        # Call LLM.
        print("Thinking...")
        try:
            response = call_llm(messages, api_base, api_key, model)
        except Exception:
            print("Failed to call LLM. Retrying in 3s...")
            time.sleep(3)
            continue

        if verbose:
            print(f"LLM response: {response}")

        # Parse action.
        action = parse_action(response)
        if action is None:
            print(f"Could not parse action from: {response}")
            messages.append({
                "role": "assistant",
                "content": response,
            })
            messages.append({
                "role": "user",
                "content": "I couldn't parse your action. Respond with exactly one JSON object.",
            })
            continue

        # Add assistant response to conversation.
        messages.append({"role": "assistant", "content": response})

        # Execute.
        status = execute_action(action)
        print(f"Action: {json.dumps(action)}")
        print(f"Result: {status}")

        if action.get("action") == "done":
            print(f"\nGoal completed: {action.get('result', '')}")
            break

        if action.get("action") == "stuck":
            print(f"\nAgent stuck: {action.get('reason', '')}")
            break

        # Wait for re-render.
        time.sleep(render_delay)

        # Keep conversation manageable — trim old screens but keep recent.
        if len(messages) > 20:
            # Keep system prompt, initial user msg, and last 8 exchanges.
            messages = messages[:2] + messages[-16:]

    else:
        print(f"\nReached max steps ({max_steps}).")

    print(f"\nBrowser still running in tmux session '{TMUX_SESSION}'.")
    print(f"  Attach: tmux attach -t {TMUX_SESSION}")
    print(f"  Kill:   tmux kill-session -t {TMUX_SESSION}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="LLM-driven web agent for Edge terminal browser",
    )
    parser.add_argument(
        "--goal", required=True,
        help="What the agent should accomplish (e.g., 'Search for weather')",
    )
    parser.add_argument(
        "--api-base",
        default=os.environ.get("OPENAI_API_BASE", "https://api.openai.com/v1"),
        help="OpenAI-compatible API base URL",
    )
    parser.add_argument(
        "--api-key",
        default=os.environ.get("OPENAI_API_KEY", ""),
        help="API key (or set OPENAI_API_KEY env var)",
    )
    parser.add_argument(
        "--model",
        default=os.environ.get("OPENAI_MODEL", "gpt-4"),
        help="Model name",
    )
    parser.add_argument(
        "--browser-cmd",
        default=DEFAULT_BROWSER_CMD,
        help="Browser launch command",
    )
    parser.add_argument(
        "--url",
        default=None,
        help="Starting URL to navigate to",
    )
    parser.add_argument(
        "--width", type=int, default=120,
        help="Terminal width in columns",
    )
    parser.add_argument(
        "--height", type=int, default=40,
        help="Terminal height in rows",
    )
    parser.add_argument(
        "--max-steps", type=int, default=30,
        help="Maximum number of agent steps",
    )
    parser.add_argument(
        "--render-delay", type=float, default=2.0,
        help="Seconds to wait after each action for re-render",
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Print screen captures and LLM responses",
    )

    args = parser.parse_args()

    if not args.api_key:
        print("Error: Set OPENAI_API_KEY or pass --api-key", file=sys.stderr)
        sys.exit(1)

    run_agent(
        goal=args.goal,
        api_base=args.api_base,
        api_key=args.api_key,
        model=args.model,
        browser_cmd=args.browser_cmd,
        width=args.width,
        height=args.height,
        max_steps=args.max_steps,
        render_delay=args.render_delay,
        start_url=args.url,
        verbose=args.verbose,
    )


if __name__ == "__main__":
    main()
