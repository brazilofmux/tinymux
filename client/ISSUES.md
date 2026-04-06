# Hydra Client—Open Issues & Feature Gaps

## Active Sub-Trackers

- **[Console](console/ISSUES.md):** Console-only Hydra reconnect and terminal-capability bugs.
- **[Android](android/ISSUES.md):** Android trigger/runtime issues.
- **[iOS](ios/ISSUES.md):** Swift Hydra transport correctness and resource-lifetime issues.
- **[TinyFugue](tf/ISSUES.md):** TF-specific Hydra transport and scripting gaps.
- **[Win32 GUI](win32gui/ISSUES.md):** Windows GUI Hydra prompt/rendering and platform-validation gaps.

## Recently Fixed

- ~~Console/Android gRPC plaintext~~ — Fixed there: TLS by default landed for those clients (1192607). TF still pending below.
- ~~Android Opens GameSession Without Input Path~~ — Fixed: bidi input (572843e)
- ~~gRPC Subscribers Steal Messages~~ — Fixed server-side: per-subscriber queues (d0d5d05)
- ~~OutputQueue Pre-rendering~~ — Fixed server-side: deferred per-subscriber rendering (634ce31)
- ~~ColorFormat Negotiation~~ — Fixed: SetPreferences on GameSession stream (6853a2e)
- ~~Auto-Reconnect Inconsistent~~ — Fixed: all clients retry with backoff (c1b7264)
- ~~Browser localStorage Session Tokens~~ — Fixed: moved to sessionStorage (bcdb9f8)

## Bugs & Security Risks

### ~~TF Hydra Transport Still Uses Plaintext gRPC~~

- **Fixed:** TF now uses SslCredentials when world has SSL flag. (3d496b9)

### ~~HTML5 Protobuf Encoder Edge Cases~~

- **Mitigated:** Proto enum shifted to COLOR_UNSPECIFIED=0 so proto3 zero-default is harmless. HTML5 Subscribe explicitly sends ANSI_TRUECOLOR=1. (3d496b9)

### ~~Android Reconnect `inputChannel` Lifecycle Bug~~

- **Fixed:** inputChannel is now @Volatile var, replaced with fresh Channel on reconnect. (ffb0026)

### ~~`fetchScrollBack()` Ignores `color_format`~~

- **Fixed:** Console and Android set color_format = ANSI_TRUECOLOR. (3096196)

### ~~TF `send_naws` Hardcodes `ColorFormat`~~

- **Fixed:** TF now tracks `currentColorFormat_` and uses it in send_naws. (3d496b9)

### ~~Web Client Uses Legacy Unary/Server-Streaming Instead of GameSession~~

- **Fixed:** WebSocket GameSession transport with `hydra-gamesession` subprotocol. Binary protobuf ClientMessage/ServerMessage frames over WS, first-message auth via SetPreferences.session_id. Legacy grpc-web path retained as fallback. (55e6bb832)

### ~~Reconnect Paths Reopen `GameSession` Without Re-Sending Preferences~~

- **Fixed:** Console and Android now resend SetPreferences on reconnect. TF still pending. (ffb0026)

## Feature Gaps

### ~~Clients Do Not Send Initial Capability Message Consistently~~

- **Fixed:** All three C++ and Android clients now send `SetPreferences` on stream open. (b8661cf)

### ~~`GetScrollBack` color_format Not Used~~

- **Fixed:** Console and Android now set color_format = ANSI_TRUECOLOR on scroll-back requests. (3096196)

### ~~Reported Terminal Size Is Still Hardcoded On Native Hydra Clients~~

- **Fixed:** All clients now send actual terminal dimensions. Console reports window size, Android estimates from screen dp, TF sends ncurses cols/rows after connect. (1d2a548, a9df518)

### ~~Browser grpc-web Path Still Cannot Choose Live Output Color Format~~

- **Fixed:** HTML5 Subscribe now sends `color_format=1` (ANSI_TRUECOLOR) explicitly. (3d496b9)

### ~~Scrollback Fetch on Reconnect~~

- **Fixed:** Console and Android now call GetScrollBack (200 lines) after successful reconnect. (46ba394)
- **Remaining:** Session persistence (saving session_id across client restart) is not yet implemented for Console/Android.

### ~~Create-Account Flow Not Exposed~~

- **Fixed:** /hcreate command added to Console, Win32GUI, and Android. (098c2c2)

### GMCP Support Is Raw JSON Only

- **Issue:** All clients format GMCP as `[GMCP Package] {json}` text.
- **Opportunity:** Provide structured hooks for common GMCP packages (Char.Vitals—vitals bar).

## Cross-Client Issues

### ~~Credential storage in plaintext~~ FIXED

- **Console/Win32 Console:** `worlds.txt` now saved with mode 0600 on Unix; load warns if permissions are loose. `secure_zero()` wipes the password from memory after Hydra authentication succeeds.
- **Win32 GUI:** Hydra passwords moved from plaintext `worlds.json` to Windows Credential Manager (`CredWriteW`/`CredReadW`). Transparent migration from existing JSON on first load; passwords stripped from JSON on next save.
- **iOS:** Already used Apple Keychain (`SecItem*` APIs) — no change needed.
- **Android:** Already used `EncryptedSharedPreferences` with AES-256-GCM — no change needed.

### ~~Spawn config regex errors silently discarded~~ FIXED

- **File:** `console/src/spawn.cpp:9-10, 24-25`
- **Issue:** `try { compiled.push_back(std::regex(...)); } catch (...) {}` silently swallows regex compilation errors. Users get no feedback when a spawn rule is invalid.

## Newly Confirmed Regressions

### ~~Console reconnect path still hardcodes `80x24`~~ FIXED

- Reconnect path now uses cached `termWidth_`/`termHeight_` instead of hardcoded 80x24, matching the TF fix.

### ~~iOS Hydra client still does not send `SetPreferences`~~ FIXED

- `runGameSession()` now sends `SetPreferences` (ANSI_TRUECOLOR, 80x24, "Titan-iOS") as the first bidi stream message. Also fixed: `EventLoopGroup` leak on repeated connect/disconnect.

### ~~SwiftUI / iOS Hydra Support Missing~~

- **Fixed:** HydraConnection.swift written with full feature parity, guarded behind #if canImport(GRPC). Needs Mac build with grpc-swift package. (6858c4d)

### ~~Hydra Command Surface Inconsistent Across Clients~~

- **Fixed:** All clients now delegate /h* commands to HydraConnection:: handleCommand(). One place to add new commands per language. (43fb05b)
