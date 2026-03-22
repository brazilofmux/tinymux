# Networking & Protocol

## Socket Layer

### NetSocket (BSD socket wrapper)
Low-level TCP socket using CFSocket for runloop integration.

- **Creation:** `+netsocketConnectedToHost:port:` with optional timeout
- **Buffering:** Separate `NSMutableData` for incoming and outgoing
- **SSL/TLS:** OpenSSL integration (`SSL_CTX`, `SSL` objects)
  - `startEncrypting` initiates handshake
  - Delegate callbacks: `netsocketSSLConnected:`, `netsocketSSLFailure:`
- **Async I/O:** CFRunLoopSource integration for non-blocking reads

### ALSocket (high-level wrapper)
Wraps NetSocket, adds SecureTransport (macOS native TLS) as alternative.

- **Notifications** (NSNotificationCenter):
  - `ALSocketDidOpenConnectionNotification`
  - `ALSocketDidNotOpenConnectionNotification`
  - `ALSocketDidTimeoutNotification`
  - `ALSocketDidReadNotification`

## Telnet Protocol (RDTelnetFilter)

Full RFC 854 implementation with state machine parser.

### State Machine
```
scanNormal → scanIAC (on 0xFF)
scanIAC → scanWILL | scanWONT | scanDO | scanDONT | scanSB
scanSB → scanSBdata → scanSBIAC (on 0xFF within subneg)
```

### Option State Tracking
Per-option state for both local and remote sides:
```
unsigned char local[256]     // Our option states
unsigned char remote[256]    // Server's option states
unsigned char localQ[256]    // Queue for pending negotiations
unsigned char remoteQ[256]
```

Values: `TQ_NO` (0), `TQ_YES` (1), `TQ_WANTNO` (2), `TQ_WANTYES` (3)

### Supported Telnet Options
| Option | Code | Notes |
|--------|------|-------|
| BINARY | 0 | Binary transmission mode |
| ECHO | 1 | Server echo (password masking) |
| SGA | 3 | Suppress Go Ahead |
| TTYPE | 24 | Terminal type negotiation |
| NAWS | 31 | Window size (width × height) |
| CHARSET | 42 | Character set negotiation (RFC 2066) |
| COMPRESS | 85 | MCCP v1 |
| COMPRESS2 | 86 | MCCP v2 |
| MSDP | 69 | MUD Server Data Protocol |

### NAWS (Window Size)
16-bit big-endian width and height. 0xFF bytes escaped as 0xFF 0xFF.

### CHARSET Negotiation (RFC 2066)

- Server sends charset list
- Client selects preferred via `CFStringConvertIANACharSetNameToEncoding()`
- Response codes: 1=request, 2=accepted, 3=rejected
- Supports TTABLE (translation table) mode

### Keepalive
Configurable modes:

- NOP command (0xF1) every 60 seconds
- Newline every 60 seconds
- Disabled

### Go Ahead / EOR

- GA (Go Ahead)—converted to prompt marker `\x1b[1a\n`
- EOR (End of Record)—same prompt marker

### Server Type Detection
Sets `mudProtocol` flag for `AtlantisServerMud` or `AtlantisServerIRE`,
which affects prompt handling behavior.

## ANSI Color Parsing (RDAnsiFilter)

### Per-Spawn State (RDAnsiState)
Each spawn maintains independent ANSI state:

- Bold, invert, underline flags
- Current foreground color (0-255)
- Current background color (0-255)
- Font pair (normal + bold)
- Holdover text for incomplete sequences
- Paragraph style, timestamp

### Color Model
| Range | Count | Meaning |
|-------|-------|---------|
| 0-7 | 8 | Standard colors |
| 8-15 | 8 | Bright/bold colors |
| 16-231 | 216 | 6×6×6 RGB cube |
| 232-255 | 24 | Grayscale ramp |

Bold on colors 0-7 shifts to 8-15. Bold on 8+ has no additional effect.
Invert swaps foreground/background while preserving color indices.

### Attribute Output
Produces `NSAttributedString` with:

- `NSFontAttributeName` — regular or bold font
- `NSForegroundColorAttributeName` — text color
- `NSBackgroundColorAttributeName` — background color
- `NSUnderlineStyleAttributeName` — underline
- `RDAnsiForegroundColor` — numeric color index (custom key)
- `RDAnsiBackgroundColor` — numeric background index (custom key)
- `RDTimeStamp` — line timestamp (custom key)

## MCCP Compression (RDCompressionFilter)

MCCP v2 (TELOPT_COMPRESS2, option 86) decompression via zlib.

- Server negotiates COMPRESS2—all subsequent data is zlib-compressed
- `z_stream` maintained for session lifetime
- Custom allocators (`zlib_calloc`, `zlib_free`)
- 0.5-second catchup timer flushes partial data
- Decompressed data replaces compressed in buffer

## Atlantis Markup Language (AML)

XML-like tag system for styled text (used in status bar, scripts).

| Tag | Purpose | Example |
|-----|---------|---------|
| `<ansi N>` | Foreground color (0-255) | `<ansi 1>Red text</ansi>` |
| `<ansi_bg N>` | Background color | `<ansi_bg 4>Blue bg</ansi_bg>` |
| `<color RRGGBB>` | HTML color | `<color FF0000>Red</color>` |
| `<bg RRGGBB>` | Background HTML color | |
| `<b>` | Bold | `<b>bold</b>` |
| `<i>` | Italic | |
| `<u>` | Underline | |
| `<link URL>` | Hyperlink | `<link http://...>click</link>` |
| `<tooltip TEXT>` | Hover tooltip | |
| `<class NAME>` | Line classification | |
| `<gag [log] [screen]>` | Suppress line | |

HTML entities: `&quot;` `&apos;` `&lt;` `&gt;` `&amp;`
