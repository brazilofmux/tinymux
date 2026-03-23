# Hydra Client — Open Issues & Feature Gaps

## Recently Fixed

- ~~Native gRPC Clients Always Disable Transport Security~~ — Fixed: TLS by default (1192607)
- ~~Android Opens GameSession Without Input Path~~ — Fixed: bidi input (572843e)
- ~~gRPC Subscribers Steal Messages~~ — Fixed server-side: per-subscriber queues (d0d5d05)
- ~~OutputQueue Pre-rendering~~ — Fixed server-side: deferred per-subscriber rendering (634ce31)
- ~~ColorFormat Negotiation~~ — Fixed: SetPreferences on GameSession stream (6853a2e)
- ~~Auto-Reconnect Inconsistent~~ — Fixed: all clients retry with backoff (c1b7264)
- ~~Browser localStorage Session Tokens~~ — Fixed: moved to sessionStorage (bcdb9f8)

## Bugs & Security Risks

### HTML5 Protobuf Encoder Edge Cases
- **Issue:** The hand-rolled browser protobuf encoder has been improved but may still have edge cases with zero-valued scalars that are semantically meaningful.
- **Opportunity:** Replace with generated code or track field presence explicitly.

### Android Reconnect `inputChannel` Lifecycle Bug
- **Issue:** `HydraConnection.kt` uses a `val inputChannel` which is closed on stream break and replaced by a *local* `newInput` channel inside the `connect()` coroutine.
- **Impact:** `sendLine()` continues to use the closed `inputChannel`, so no input is sent to the server after the first reconnect.
- **Fix:** Use a thread-safe way to swap channels or keep the channel alive across reconnects.

### `fetchScrollBack()` Ignores `color_format`
- **Issue:** Console and Android clients do not set `color_format` in the `ScrollBackRequest`.
- **Impact:** Server (even when fixed) may return output in an undesired format.
- **Fix:** Set the preferred `ColorFormat` in the request.

### TF `send_naws` Hardcodes `ColorFormat`
- **Issue:** `HydraConnection::send_naws` always sends `ANSI_TRUECOLOR`.
- **Impact:** Reseting preferences may revert the color rendering to TrueColor if the user had manually requested another format.
- **Fix:** Track the current preferred format and use it for all `SetPreferences` messages.

### Web Client Uses Legacy Unary/Server-Streaming Instead of GameSession
- **Issue:** Browser client uses `SendInput` + `Subscribe` instead of the bidi `GameSession`.
- **Impact:** The web path diverges from the primary protocol. Features added to `GameSession` only (like `SetPreferences`) don't reach browser clients.
- **Note:** This is a grpc-web limitation — true bidi streaming requires WebSocket-based gRPC.

## Feature Gaps

### ~~Clients Do Not Send Initial Capability Message Consistently~~
- **Fixed:** All three C++ and Android clients now send `SetPreferences` on stream open. (b8661cf)

### ~~`GetScrollBack` color_format Not Used~~
- **Fixed:** Console and Android now set color_format = ANSI_TRUECOLOR on scroll-back requests. (3096196)

### ~~Scrollback Fetch on Reconnect~~
- **Fixed:** Console and Android now call GetScrollBack (200 lines) after successful reconnect. (46ba394)
- **Remaining:** Session persistence (saving session_id across client restart) is not yet implemented for Console/Android.

### ~~Create-Account Flow Not Exposed~~
- **Fixed:** /hcreate command added to Console, Win32GUI, and Android. (098c2c2)

### GMCP Support Is Raw JSON Only
- **Issue:** All clients format GMCP as `[GMCP Package] {json}` text.
- **Opportunity:** Provide structured hooks for common GMCP packages (Char.Vitals → vitals bar).

### ~~SwiftUI / iOS Hydra Support Missing~~
- **Fixed:** HydraConnection.swift written with full feature parity, guarded behind #if canImport(GRPC). Needs Mac build with grpc-swift package. (6858c4d)

### ~~Hydra Command Surface Inconsistent Across Clients~~
- **Fixed:** All clients now delegate /h* commands to HydraConnection::handleCommand(). One place to add new commands per language. (43fb05b)
