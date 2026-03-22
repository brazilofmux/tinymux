# Filter Pipeline

## Architecture

Abstract base class `RDAtlantisFilter` with two entry points:
```objc
- (void)filterInput:(id)input;    // Process incoming data (server → client)
- (void)filterOutput:(id)output;  // Process outgoing data (client → server)
```

Filters compose into an ordered chain. Each filter receives a reference
to its world instance (`initWithWorld:`).

## Filter Chain Order (Input)

```
1. RDTelnetFilter     — IAC parsing, option negotiation, keepalive
2. RDCompressionFilter — MCCP v2 zlib decompression
3. RDMCPFilter         — MCP #$# packet extraction
4. RDAnsiFilter        — ANSI escape codes → styled text
5. RDURLFilter         — Detect and linkify URLs
6. RDEmailFilter       — Detect and linkify email addresses
```

## Filter Chain Order (Output)

```
1. RDTelnetFilter      — IAC escaping (0xFF → 0xFF 0xFF)
2. RDCompressionFilter — (if MCCP compression active)
```

## Filter Details

### RDTelnetFilter
See [02-networking.md](02-networking.md) for full details.

- State machine for IAC parsing
- Option negotiation (WILL/WONT/DO/DONT)
- Subnegotiation handling
- NAWS, TTYPE, CHARSET support
- MCCP trigger (hands off to compression filter)
- Keepalive timer

### RDCompressionFilter

- Handles MCCP v2 (option 86) decompression
- zlib `inflateInit()` / `inflate()` stream
- 0.5-second catchup timer for partial data
- Custom memory allocators

### RDMCPFilter
See [07-mcp.md](07-mcp.md) for full details.

- Detects `#$#` prefix lines
- Parses MCP messages (key-value pairs)
- Handles multiline messages (data tags)
- Legacy MCP 1.0 compatibility
- Routes to MCPDispatch

### RDAnsiFilter
See [02-networking.md](02-networking.md) for color details.

- Per-spawn RDAnsiState object
- Parses ESC[ sequences
- Produces NSAttributedString with color/style attributes
- Handles holdover (incomplete sequences across packet boundaries)

### RDURLFilter

- Scans text for URL patterns (http://, https://, etc.)
- Adds NSLinkAttributeName to matching ranges
- Makes URLs clickable in RDTextView

### RDEmailFilter

- Scans text for email address patterns
- Adds mailto: link annotations
- Makes emails clickable

## Integration Points

- Filters instantiated per world instance
- Stored in `_rdInputFilters` array on RDAtlantisWorldInstance
- World calls filters in order on each incoming data chunk
- Filters may modify data in-place (NSMutableData / NSMutableAttributedString)
- Filters observe NSNotificationCenter for world refresh events
