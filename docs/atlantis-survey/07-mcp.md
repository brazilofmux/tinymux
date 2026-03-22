# MCP (MUD Client Protocol)

## Overview

MCP provides structured communication between MUD server and client
beyond raw text. Used for remote editing, negotiation, and metadata.

Atlantis supports MCP 1.0 (legacy) and MCP 2.11.

## Message Format

```
#$# <namespace-command> <auth-key> <key1>: <value1> <key2>: <value2> ...
```

Multiline messages:
```
#$# <namespace-command> <auth-key> _data-tag: <tag> key*: "" ...
#$#* <tag> key: line1
#$#* <tag> key: line2
#$#: <tag>
```

## Classes

### MCPMessage
Parsed MCP packet representation:

- `_rdNamespace` — package namespace (e.g., "dns-org-mud-moo-simpleedit")
- `_rdCommand` — subcommand
- `_rdAttributes` — key-value dictionary
- `_rdAttributeOrder` — preserves attribute order
- `_rdSessionKey` — authentication key
- `_rdFinished` — message complete flag
- `_rdCompat10` — MCP 1.0 mode

Key methods:

- `addText:toAttribute:` — append to attribute value
- `makeAttributeMultiline:` — mark attribute as multiline
- `attributeText:` — get single-line value
- `attributeLines:` — get multiline value as array
- `messageString` — serialize back to wire format

### MCPDispatch
Central dispatcher (singleton-like per controller):

- `registerHandler:forNamespace:` — add handler
- `dispatchMessage:forState:` — route to handler
- `handlerForNamespace:` — lookup by longest prefix

### MCPHandler (abstract base)

- `handleMessage:withState:` — process incoming message
- `minVersion:` / `maxVersion:` — version range per namespace
- `negotiated:state` — callback after successful negotiation

### MCPNegotiateHandler
Built-in handler for the `mcp` core namespace:

- Version negotiation
- Package list exchange
- Session key validation

### MCPEditHandler
Built-in handler for text editing:

- Receives edit requests from server
- Opens local editor (MCPLocalEditor panel)
- Sends modified text back on save/close

## Integration

1. RDMCPFilter detects `#$#` prefix in incoming text
2. Parses into MCPMessage objects
3. Validates session key
4. MCPDispatch routes to appropriate MCPHandler
5. Handler processes (e.g., opens editor panel)

## Known MCP Packages

| Package | Handler | Purpose |
|---------|---------|---------|
| mcp | MCPNegotiateHandler | Core negotiation |
| mcp.edit | MCPEditHandler | Remote text editing |
| (VMoo) | MCPVMooClient | VMoo editor protocol |
| (Icecrew) | MCPIcecrewServer | Icecrew editor protocol |

## Session Keys

16-character authentication tokens generated per connection.
All MCP messages must include the correct session key.
Prevents unauthorized MCP packet injection by other text in the stream.
