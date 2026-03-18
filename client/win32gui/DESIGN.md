# Win32 GUI MU* Client — Design Document

## 1. Purpose

A clean, modern, lightweight Win32 GUI MU* client that demonstrates
TinyMUX's capabilities (Unicode 16.0, truecolor, Schannel TLS) while
getting out of the way.  Not a MUSHClient replacement for power users.
Not a plugin platform.  A reference implementation that does the basics
right.

If there is a conflict between "reference implementation" and "feature
richness," reference wins.

## 2. Design Decisions

| # | Question | Decision |
|---|----------|----------|
| 1 | Window model | **Tabs** with activity indicators (SimpleMU style) |
| 2 | Output pane | **Custom-drawn** (own the paint, not Rich Edit) |
| 3 | Input area | **Multi-line** (1–3 rows, auto-grow like TitanFugue) |
| 4 | Font | **Per-world setting**, monospace default |
| 5 | Color pipeline | **New `co_render_attrs()`** — structured color, no ANSI escapes |
| 6 | Selection/copy | **Character selection**, plain text copy |
| 7 | Settings | **JSON** config files (no registry) |
| 8 | Target | Clean + reference (B + C from strategy doc) |

## 3. Architecture

### 3.1 CBT Hook Pattern (from client/win)

The existing `client/win/fidget.cpp` uses a `WH_CBT` hook to intercept
`HCBT_CREATEWND` and associate a C++ object pointer with each HWND via
`SetWindowLongPtr(GWLP_USERDATA)`.  This lets every window procedure
retrieve its C++ `this` pointer without MFC, ATL, or static maps.

We reuse this pattern.  The hook is installed once at startup, removed
at shutdown.  Every window class gets a static `WndProc` that calls
through to a C++ method.

### 3.2 Class Hierarchy

```
CWindow              Base: wraps HWND, stores m_hwnd + parent pointer,
│                    provides DefWindowProc/message routing.
│
├── CMainFrame       Top-level frame.  Owns the tab control, menu bar,
│                    and status bar.  Routes WM_COMMAND.
│
├── CTabBar          Owner-drawn tab control along the top.  One tab per
│                    world connection.  Activity dots, close buttons.
│
├── COutputPane      Custom-drawn scrollback output.  Owns the line
│                    buffer, selection state, scroll position.  Paints
│                    via GDI ExtTextOut with colors from co_render_attrs.
│
├── CInputPane       Multi-line edit area (1–3 rows).  Handles key
│                    input, history, cursor movement.  Grapheme-cluster
│                    aware via libmux co_cluster_* functions.
│
├── CStatusBar       Bottom status bar.  World name, idle, connection
│                    count, activity, logging indicator.
│
└── CSettingsDialog  Modal dialog for world definitions, trigger
                     management, font/color preferences.
```

### 3.3 Window Layout

```
┌─────────────────────────────────────────────┐
│ Menu Bar                                     │
├──┬──────────┬──────────┬────────────────────┤
│  │ World A  │ World B• │ World C            │  ← Tab bar (• = activity)
├──┴──────────┴──────────┴────────────────────┤
│                                              │
│  Output pane (custom-drawn scrollback)       │
│                                              │
│  Lines of MUD output with PUA color          │
│  rendered via co_render_attrs → GDI.         │
│                                              │
│  Character selection with mouse.             │
│  PgUp/PgDn for scrollback.                   │
│                                              │
├──────────────────────────────────────────────┤
│ > input line (1-3 rows, auto-grow)           │  ← Input pane
├──────────────────────────────────────────────┤
│ WorldA [ssl] idle:42s  [3 conn, 1 active]    │  ← Status bar
└──────────────────────────────────────────────┘
```

### 3.4 Per-World State

Each tab owns:

- A `Connection` (from the console client — IOCP, Schannel, telnet)
- An `OutputBuffer` — the scrollback line array + color attribute arrays
- Input history (per-world)
- Scroll position
- Selection state
- Font override (if configured)
- GMCP/MSSP data
- Log file handle (if logging)

Tab switching swaps which `OutputBuffer` the `COutputPane` paints.

## 4. Color Rendering Pipeline

### 4.1 The Problem

The existing `co_render_truecolor()` etc. output ANSI escape sequences.
A GUI has no VT terminal.  We need structured color data that maps
directly to GDI `SetTextColor` / `SetBkColor` calls.

### 4.2 Proposed API: `co_render_attrs()`

**Request for Ubuntu agent: implement this in `color_ops.rl` / `color_ops.c`.**

```c
/*
 * co_color_attr — Per-character color and style attributes.
 *
 * Used by GUI rendering paths that need structured color data
 * instead of ANSI escape sequences.
 */
typedef struct {
    uint32_t fg;        /* 0x00RRGGBB for truecolor, or indexed (0-255) */
    uint32_t bg;        /* 0x00RRGGBB for truecolor, or indexed (0-255) */
    uint8_t  bold;      /* 1 = bold/intense */
    uint8_t  underline; /* 1 = underline */
    uint8_t  blink;     /* 1 = blink (may be rendered as slow-pulse or ignored) */
    uint8_t  inverse;   /* 1 = fg/bg swapped */
    uint8_t  fg_type;   /* 0 = default, 1 = indexed (0-255), 2 = RGB */
    uint8_t  bg_type;   /* 0 = default, 1 = indexed (0-255), 2 = RGB */
    uint8_t  pad[2];    /* Alignment padding */
} co_color_attr;

/*
 * co_render_attrs — Strip PUA color from UTF-8, emit text + parallel attrs.
 *
 * Walks PUA-encoded input, strips all color code points, writes visible
 * UTF-8 bytes to out_text, and writes one co_color_attr per output byte
 * to out_attrs.  All bytes within a single visible code point share the
 * same attribute entry.
 *
 * The caller groups consecutive bytes with identical co_color_attr into
 * "runs" and draws each run in one GDI/Direct2D call.
 *
 * out_attrs:  Must have room for at least LBUF_SIZE entries.
 * out_text:   Must have room for at least LBUF_SIZE bytes.
 * data:       PUA-encoded UTF-8 input.
 * len:        Length of data in bytes.
 * bNoBleed:   If nonzero, reset color state to default at end of line.
 *
 * Returns bytes written to out_text.  out_attrs[0..return-1] is valid.
 */
LIBMUX_API size_t co_render_attrs(co_color_attr *out_attrs,
                                  unsigned char *out_text,
                                  const unsigned char *data, size_t len,
                                  int bNoBleed);
```

### 4.3 GUI Paint Flow

```
Server → telnet → PUA-encoded UTF-8 line
                       │
                       ▼
              co_render_attrs()
              ┌─────────────────┐
              │ out_text: "Hello"│  (visible UTF-8, no PUA)
              │ out_attrs: [5]   │  (parallel color per byte)
              └─────────────────┘
                       │
                       ▼
              OutputBuffer::append()
              Stores text + attrs as a line entry.
                       │
                       ▼
              COutputPane::OnPaint()
              Groups consecutive identical attrs into runs.
              For each run:
                SetTextColor(hdc, RGB(attr.fg))
                SetBkColor(hdc, RGB(attr.bg))
                ExtTextOutW(hdc, x, y, ..., run_text, run_len, ...)
                Advance x by run display width.
```

### 4.4 Default Colors

When `fg_type == 0` (default foreground), the GUI uses the user's
configured default text color (white-on-black, or per-world setting).
When `bg_type == 0`, the GUI uses the configured background.

Indexed colors (0-255) are resolved to RGB via the xterm-256 palette
at paint time, not in `co_render_attrs`.  This allows the user to
customize the palette per-world without re-rendering stored lines.

## 5. Output Buffer

### 5.1 Line Storage

```cpp
struct OutputLine {
    std::string text;                   // Visible UTF-8 (PUA stripped)
    std::vector<co_color_attr> attrs;   // Parallel color, same length as text
    int display_width;                  // Column count (via co_visual_width)
};

class OutputBuffer {
    std::deque<OutputLine> lines;
    int scroll_offset = 0;
    static constexpr size_t MAX_LINES = 20000;
};
```

### 5.2 Word Wrap

Word wrap is computed at paint time, not at insertion time.  This means
resize doesn't require re-wrapping the entire buffer — just repainting
the visible portion.  The wrap algorithm walks the line's text by
grapheme cluster (via `co_cluster_advance`) and tracks display column
width (via `co_console_width`).

## 6. Input Pane

### 6.1 Behavior

- 1 row by default.  Grows to 2–3 rows as the user types long input.
- Enter sends.  Shift+Enter inserts a literal newline (for multi-line).
- Up/Down navigate command history (per-world).
- Ctrl+A/E/K/U for emacs-style line editing.
- Grapheme-cluster-aware cursor movement via `co_cluster_advance`.

### 6.2 Implementation

The input pane is a custom-drawn control, not an Edit control.  This
gives us full control over:

- Grapheme cluster navigation (Win32 Edit doesn't understand UAX #29)
- Display width calculation for CJK characters
- Consistent behavior across Windows versions

## 7. Tab Bar

### 7.1 Appearance

Owner-drawn tabs along the top of the client area.  Each tab shows:

- World name (truncated with ellipsis if needed)
- Activity indicator (filled dot or highlight color) for background output
- Close button (× glyph) on hover
- SSL padlock icon for TLS connections

### 7.2 Behavior

- Click to switch foreground world
- Middle-click to close
- Ctrl+Tab / Ctrl+Shift+Tab to cycle
- Tabs reorderable by drag (stretch goal)
- Right-click context menu: Close, Close Others, Reconnect

## 8. Menu Bar

```
File
  Connect...          Ctrl+N     (world picker dialog)
  Disconnect          Ctrl+D
  ─────────
  Log to File...      Ctrl+L
  Stop Logging
  ─────────
  Settings...         Ctrl+,
  ─────────
  Exit                Alt+F4

Edit
  Copy                Ctrl+C
  Paste               Ctrl+V
  Select All          Ctrl+A
  ─────────
  Find in Scrollback  Ctrl+F

View
  Scroll to Bottom    Ctrl+End
  Clear Output        (confirm dialog)
  ─────────
  Font...             (per-world font picker)
  ─────────
  Toggle Status Bar

World
  Reconnect
  ─────────
  Triggers...         (trigger editor dialog)
  Timers...           (timer editor dialog)
  Key Bindings...     (keybind editor dialog)
  ─────────
  GMCP Data           (read-only viewer)
  MSSP Data           (read-only viewer)

Help
  About
```

## 9. Settings (JSON)

### 9.1 File Location

`%APPDATA%\TinyMUX\client.json` — global settings.
`%APPDATA%\TinyMUX\worlds.json` — world definitions.
`%APPDATA%\TinyMUX\triggers.json` — trigger/macro definitions.
`%APPDATA%\TinyMUX\keybindings.json` — keybinding overrides.

### 9.2 Global Settings Schema

```json
{
  "font": {
    "name": "Consolas",
    "size": 12
  },
  "colors": {
    "default_fg": "#C0C0C0",
    "default_bg": "#000000"
  },
  "scrollback_lines": 20000,
  "prompt_timeout_ms": 500,
  "show_status_bar": true,
  "tab_close_button": true
}
```

### 9.3 World Definition Schema

```json
{
  "worlds": [
    {
      "name": "MyMUX",
      "host": "mux.example.com",
      "port": "4201",
      "ssl": true,
      "character": "Player",
      "auto_connect": false,
      "font": {
        "name": "Lucida Console",
        "size": 10
      },
      "encoding": "UTF-8"
    }
  ]
}
```

## 10. Networking

Reuse the console client's `Connection` class directly:

- IOCP event loop (same pattern — but the GUI message loop replaces
  `GetQueuedCompletionStatus` with `MsgWaitForMultipleObjectsEx`)
- Schannel TLS
- Telnet state machine (NAWS, TTYPE, CHARSET, GMCP, MSSP)
- The Ragel trigger engine (`trigger_match.c`)

### 10.1 Event Loop Integration

Win32 GUI apps use a message pump.  The IOCP needs to interleave with
Windows messages.  The approach:

```cpp
for (;;) {
    DWORD result = MsgWaitForMultipleObjectsEx(
        1, &hIOCP,
        timer_ms,              // next timer or 100ms
        QS_ALLINPUT,
        MWMO_ALERTABLE | MWMO_INPUTAVAILABLE
    );

    if (result == WAIT_OBJECT_0) {
        // IOCP completion — drain all pending completions
        drain_iocp(app);
    } else if (result == WAIT_OBJECT_0 + 1) {
        // Windows messages pending
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } else if (result == WAIT_TIMEOUT) {
        // Timer tick — prompt detection, timer firing, status update
        check_prompts(app);
        fire_timers(app);
        update_status(app);
    }
}
```

This gives IOCP completions equal priority with UI events.  No
dedicated input thread needed (unlike the console client) because
`MsgWaitForMultipleObjectsEx` handles both.

## 11. Build

Visual Studio 2026 (PlatformToolset v145), x64 only, C++17.
Links `libmux.lib` for Unicode/color operations.
Post-build copies `libmux.dll` to output directory.

Solution: `client/win32gui/win32gui.sln`
Project: `client/win32gui/win32gui.vcxproj`
Output: `client/win32gui/bin/Release/tinymux-client.exe`

## 12. File Structure

```
client/win32gui/
    DESIGN.md               This document
    win32gui.sln
    win32gui.vcxproj
    res/
        app.ico             Application icon
        app.rc              Resource script (menus, dialogs, strings)
        resource.h          Resource IDs
    src/
        main.cpp            Entry point, message pump, IOCP integration
        app.h / app.cpp     Application state (worlds, connections, settings)
        window.h/cpp        CWindow base class (CBT hook pattern from client/win)
        mainframe.h/cpp     CMainFrame — top-level window, menu, layout
        tabbar.h/cpp        CTabBar — owner-drawn tab control
        outputpane.h/cpp    COutputPane — custom-drawn scrollback output
        inputpane.h/cpp     CInputPane — multi-line input editor
        statusbar.h/cpp     CStatusBar — bottom status bar
        outputbuffer.h      OutputLine, OutputBuffer
        settings.h/cpp      JSON config load/save
        dialogs.h/cpp       Settings, Connect, About dialogs
```

Connection, telnet, Schannel, macro, keybind, timer, and world
modules are shared with the console client (included from
`client/console/src/`), or factored into a shared location.

## 13. Implementation Phases

### Phase 1: Window Shell

CWindow + CBT hook, CMainFrame with menu, empty COutputPane +
CInputPane + CStatusBar.  Tab bar with dummy tabs.  Compiles, runs,
shows the window layout.  No networking.

### Phase 2: Rendering

`co_render_attrs()` integration (requires Ubuntu agent).  OutputBuffer
stores rendered lines.  COutputPane paints lines with GDI ExtTextOut.
Scrollback with PgUp/PgDn.  Selection and Ctrl+C copy.

### Phase 3: Networking

Port Connection/Schannel/Telnet.  IOCP + MsgWaitForMultipleObjectsEx
event loop.  Connect dialog.  Lines flow from server to OutputBuffer
to paint.  Input pane sends to server.

### Phase 4: Tabs + Multi-World

Tab bar wired to connections.  Per-world OutputBuffer, history,
scroll position.  Activity indicators.  Ctrl+Tab switching.

### Phase 5: Settings + Dialogs

JSON config load/save.  Settings dialog (font, colors, scrollback).
World editor dialog.  Trigger editor dialog.  Per-world font override.

### Phase 6: Polish

Word wrap.  URL detection.  Find in scrollback.  Window
position/size persistence.  Keyboard accelerators.  About dialog.
