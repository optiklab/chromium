#!/usr/bin/env bash
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# secure-browse: Disposable browser session over SSH/terminal.
#
# Creates a temporary profile, launches Edge in terminal mode inside tmux,
# and wipes all browsing data on exit. Nothing persists. The terminal
# stream is plain text — no pixels leave the server.
#
# Usage:
#   ssh server "secure-browse"                  # interactive
#   ssh -t server "secure-browse --url https://example.com"
#   secure-browse --session work --keep         # named, persistent

set -euo pipefail

# --- Configuration -----------------------------------------------------------

EDGE_BIN="${EDGE_BIN:-./out/linux_x64_debug_developer_build/msedge}"
SESSION_NAME=""
KEEP_DATA=false
START_URL=""
CELL_SIZE="4x8"
RENDER_MODE="halfblock"
EXTRA_FLAGS=""

# --- Parse arguments ----------------------------------------------------------

usage() {
    cat <<'EOF'
secure-browse — Disposable browser isolation over terminal/SSH

Usage: secure-browse [OPTIONS] [URL]

Options:
  --session NAME    tmux session name (default: edge-secure-RANDOM)
  --keep            Don't wipe profile on exit (for named sessions)
  --cell-size WxH   Terminal cell pixel size (default: 4x8)
  --render-mode M   halfblock or sixel (default: halfblock)
  --edge-bin PATH   Path to msedge binary
  -h, --help        Show this help

Environment:
  EDGE_BIN          Path to msedge binary (default: ./out/.../msedge)

Security features (always enabled):
  • Fresh temporary profile (wiped on exit unless --keep)
  • Extensions disabled
  • Sync disabled
  • Autofill disabled
  • Password saving disabled
  • Translation disabled
  • Background networking minimized
  • No first-run experience
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --session)   SESSION_NAME="$2"; shift 2 ;;
        --keep)      KEEP_DATA=true; shift ;;
        --cell-size) CELL_SIZE="$2"; shift 2 ;;
        --render-mode) RENDER_MODE="$2"; shift 2 ;;
        --edge-bin)  EDGE_BIN="$2"; shift 2 ;;
        -h|--help)   usage ;;
        -*)          echo "Unknown option: $1" >&2; exit 1 ;;
        *)           START_URL="$1"; shift ;;
    esac
done

# --- Validate -----------------------------------------------------------------

if [[ ! -x "$EDGE_BIN" ]]; then
    echo "Error: Edge binary not found at: $EDGE_BIN" >&2
    echo "Set EDGE_BIN or pass --edge-bin PATH" >&2
    exit 1
fi

if ! command -v tmux &>/dev/null; then
    echo "Error: tmux is required. Install with: apt install tmux" >&2
    exit 1
fi

# --- Setup --------------------------------------------------------------------

# Generate session name if not provided.
if [[ -z "$SESSION_NAME" ]]; then
    SESSION_NAME="edge-secure-$(head -c 4 /dev/urandom | xxd -p)"
fi

# Create temporary profile directory.
PROFILE_DIR=$(mktemp -d "/tmp/${SESSION_NAME}-profile-XXXXXX")
echo "Profile: $PROFILE_DIR"

# Cleanup function — wipe profile on exit unless --keep.
cleanup() {
    if [[ "$KEEP_DATA" == false ]]; then
        echo ""
        echo "Wiping browsing data: $PROFILE_DIR"
        rm -rf "$PROFILE_DIR"
        echo "All browsing data destroyed."
    else
        echo ""
        echo "Profile preserved at: $PROFILE_DIR"
    fi
}
trap cleanup EXIT

# --- Security-hardened browser flags ------------------------------------------

BROWSER_FLAGS=(
    --ozone-platform=terminal
    --enable-features=msTerminalPlatform
    --terminal-cell-size="$CELL_SIZE"
    --user-data-dir="$PROFILE_DIR"

    # Disable first-run experience.
    --no-first-run
    --no-default-browser-check

    # Disable data-leaking features.
    --disable-sync
    --disable-background-networking
    --disable-extensions
    --disable-component-extensions-with-background-pages
    --disable-default-apps
    --disable-translate

    # Disable autofill and password saving.
    --disable-features=AutofillServerCommunication,PasswordManagerEnabled

    # Disable crash reporting (no data sent off-machine).
    --disable-breakpad
    --disable-crash-reporter

    # Disable metrics/telemetry.
    --no-pings
    --metrics-recording-only

    # DNS-over-HTTPS for privacy (Cloudflare).
    --doh-url=https://cloudflare-dns.com/dns-query
)

if [[ "$RENDER_MODE" != "halfblock" ]]; then
    BROWSER_FLAGS+=(--terminal-render-mode="$RENDER_MODE")
fi

if [[ -n "$START_URL" ]]; then
    BROWSER_FLAGS+=("$START_URL")
fi

# --- Launch -------------------------------------------------------------------

echo "Session:  $SESSION_NAME"
echo "Binary:   $EDGE_BIN"
echo "URL:      ${START_URL:-<new tab>}"
echo "Cleanup:  $(if $KEEP_DATA; then echo 'manual (--keep)'; else echo 'automatic on exit'; fi)"
echo ""
echo "Launching secure browser session..."
echo "  Detach: Ctrl-B D    Reattach: tmux attach -t $SESSION_NAME"
echo ""

# Get terminal dimensions.
COLS=$(tput cols 2>/dev/null || echo 120)
ROWS=$(tput lines 2>/dev/null || echo 40)

# Kill existing session with same name if any.
tmux kill-session -t "$SESSION_NAME" 2>/dev/null || true

# Create tmux session with the browser.
tmux new-session \
    -s "$SESSION_NAME" \
    -x "$COLS" \
    -y "$ROWS" \
    "$EDGE_BIN ${BROWSER_FLAGS[*]}"

# When tmux exits (browser closed or session killed), cleanup runs via trap.
