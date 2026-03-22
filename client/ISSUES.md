# Hydra Client — Open Issues & Feature Gaps

## Bugs & Security Risks

### Native gRPC Clients Always Disable Transport Security
- **Issue:** The Windows console client, TF client, and Android client all hard-code insecure/plaintext gRPC channels.
- **Evidence:** `client/console/src/hydra_connection.cpp:47-50`, `client/tf/src/hydra_connection.cpp:77-80`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:50-54`
- **Impact:** Hydra credentials and resumable session tokens are sent in cleartext whenever these clients are used over a real network, which defeats one of Hydra's primary reasons to exist.
- **Opportunity:** Add TLS-by-default channel construction plus hostname verification and an explicit opt-out for local development only.

### Android Opens `GameSession` Without Any Input Path On The Stream
- **Issue:** The Android `GameSession` stream only emits periodic pings; real user input is sent through separate unary `SendInput` RPCs.
- **Evidence:** `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:104-120`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:171-183`
- **Impact:** The bidi stream is not being used as designed, adds avoidable RPC churn, and makes it harder to evolve the protocol around ordered input, flow control, and stream-scoped capabilities.

### Browser Client Persists Bearer Session Tokens In `localStorage`
- **Issue:** The web Hydra client stores the resumable `sessionId` in `localStorage` and reuses it automatically on reload.
- **Evidence:** `client/web/js/hydra_connection.js:404-429`, `client/web/js/hydra_connection.js:447-448`
- **Impact:** Any XSS bug in the web client can exfiltrate long-lived Hydra session tokens. Shared-browser usage also leaves resumable sessions behind after the tab closes.
- **Opportunity:** Move to `sessionStorage`, short-lived refreshable tokens, or an HttpOnly cookie-based front door if browser resume matters.

### Web Client Still Uses Legacy Unary/Server-Streaming RPCs Instead Of `GameSession`
- **Issue:** The browser client sends text through `SendInput` and receives output through `Subscribe`; it never opens the advertised bidi `GameSession`.
- **Evidence:** `client/web/js/hydra_connection.js:541-548`, `client/web/js/hydra_connection.js:802-864`
- **Impact:** The web path is already diverging from the primary Hydra protocol. Features added only to `GameSession` will immediately miss the browser client, and ordering between input and streamed output is weaker.

### HTML5 Protobuf Encoder Drops Explicit `0` / `false` Values
- **Issue:** The hand-rolled browser protobuf encoder suppresses proto3 scalar fields whose value is `0` or `false`, except where enums are manually special-cased.
- **Evidence:** `client/web/js/hydra_connection.js:46-84`
- **Impact:** Any future grpc-web request that needs to intentionally send `0` or `false` as a meaningful scalar will be serialized incorrectly. This is a latent correctness bug in the transport layer.
- **Opportunity:** Replace the custom codec with generated code, or track field presence explicitly instead of assuming zero-values are always safe to omit.

### Auto-Reconnect On Stream Failure Is Still Not Consistent Across Clients
- **Issue:** Web and TF implement reconnect loops, but Console and Android still treat a broken `GameSession` stream as terminal.
- **Evidence:** `client/web/js/hydra_connection.js:800-888`, `client/tf/src/hydra_connection.cpp:684-780`, `client/console/src/hydra_connection.cpp:202-222`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:153-160`
- **Impact:** Hydra sessions can survive a transient network break while the user-facing client still drops the session on the floor. This remains a Tier 1 continuity issue on the clients without reconnect.

## Feature Gaps

### Hydra Command Handling Exists But Is Still Inconsistent Across Clients
- **Issue:** Hydra command support is no longer absent, but it is implemented in different layers on different clients: TF and web intercept inside the transport, Android routes commands in UI code, and Console exposes RPC helpers through separate command handling.
- **Evidence:** `client/tf/src/hydra_connection.cpp:145-223`, `client/web/js/hydra_connection.js:476-539`, `client/android/app/src/main/java/org/tinymux/titan/ui/TitanApp.kt:338-389`, `client/console/src/command.cpp:763-833`
- **Impact:** The feature exists, but it is easy for clients to drift. Adding a new Hydra command will require touching multiple disconnected integration points and risks parity bugs.
- **Opportunity:** Standardize a shared Hydra command surface and keep command dispatch in a consistent layer across client families.

### Lack of `ColorFormat` Negotiation (gRPC)
- **Issue:** All gRPC clients (Console, Android) currently receive TrueColor ANSI output regardless of their actual support or configuration.
- **Root Cause:** The `hydra.proto` `GameSession` RPC does not provide a way for the client to request a specific `ColorFormat`.
- **Impact:** Potential for broken output on older terminals or clients that expect PLAIN text or 256-color only.

### Inconsistent Input Transport (Android)
- **Issue:** The Android `HydraConnection.kt` uses `stub.sendInput` (unary RPC) for outgoing text, rather than using the `GameSession` bi-directional stream's `input_line` field.
- **Impact:** Unnecessary overhead from creating separate RPC calls for every line of input.

### Terminal Size & Type Reporting (NAWS/TTYPE)
- **Issue:** gRPC-based clients do not report their terminal size (NAWS) or type (TTYPE) to Hydra.
- **Impact:** The proxy uses default settings (80x24, "Hydra") for the back-door link to the game, causing line-wrapping issues on the game-side if the client's actual window is different.

### `GetScrollBack` Capability Fields Are Never Used By Clients
- **Issue:** The protocol exposes `ScrollBackRequest.color_format`, but the current web and TF client requests only set `session_id` and `max_lines`.
- **Evidence:** `proxy/hydra.proto:260-268`, `client/web/js/hydra_connection.js:633-640`, `client/tf/src/hydra_connection.cpp:454-466`
- **Impact:** Historical output cannot be tailored to the actual renderer, which means scrollback behavior will diverge from live output once color negotiation is fixed.

### Scrollback Fetch And Session Resume Remain Uneven Across Clients
- **Issue:** Web now persists the Hydra session token and fetches scrollback, TF fetches scrollback on demand, but Console and Android still have no comparable resumable-session persistence path and no reconnect-time history fetch.
- **Evidence:** `client/web/js/hydra_connection.js:404-448`, `client/web/js/hydra_connection.js:633-645`, `client/tf/src/hydra_connection.cpp:453-468`, `client/console/src/hydra_connection.cpp`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt`
- **Impact:** Session continuity varies sharply by client family. A browser reload may resume cleanly, while a native client restart still falls back to a fresh login path.

### Create-Account Flow Is Not Exposed Consistently In Hydra Clients
- **Issue:** The service defines `CreateAccount`, but current Hydra-capable clients still appear to be authenticate-first rather than offering a first-run account creation flow comparable to the telnet bootstrap path.
- **Evidence:** `proxy/hydra.proto:139-147`, `proxy/hydra.proto:347`, `client/console/src/hydra_connection.cpp`, `client/tf/src/hydra_connection.cpp`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt`, `client/web/js/hydra_connection.js`
- **Impact:** A new user on a Hydra-only client can be blocked from onboarding unless account creation happens through another interface first.

### SSL/TLS Verification
- **Issue:** Many clients (e.g., `TofuCertStore.kt` in Android) use a "Trust On First Use" (TOFU) model or skip certificate verification entirely.
- **Opportunity:** Implement standard certificate pinning or system trust store verification for connections to the Hydra proxy.

### Clients Do Not Send Any Initial Stream-Scoped Capability Message
- **Issue:** Console, TF, and Android open `GameSession` with only `authorization` metadata and immediately start reading or sending pings/input.
- **Evidence:** `client/console/src/hydra_connection.cpp:97-113`, `client/tf/src/hydra_connection.cpp:116-130`, `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:104-120`
- **Impact:** There is currently no place to negotiate color depth, charset preferences, viewport size, terminal type, or client feature flags on the main interactive stream.

## Implementation Details

### gRPC vs. Telnet Parity
- **Observation:** Traditional telnet clients (like TF, Mudlet, MUSHclient) have a more mature feature set for terminal negotiation than current gRPC client implementations.
- **Goal:** Reach parity by adding structured terminal capability reporting to the `GameSession` protocol.

### GMCP Support
- **Issue:** Clients handle GMCP as raw JSON strings and often just log them to the output buffer for debugging.
- **Opportunity:** Provide structured hooks or a registration mechanism for GMCP packages in the client-side library (e.g., automatically updating a vitals bar when `Char.Vitals` arrives).

### SwiftUI / iOS Hydra Support Still Appears To Be Missing
- **Observation:** The current Hydra work covers Console, Win32GUI, TF, web, and Android, but this review did not surface an equivalent iOS Hydra transport/client path.
- **Impact:** Hydra capability remains platform-incomplete even if other clients reach parity.
- **Opportunity:** Add a native iOS Hydra connection layer and match the session/command UX being built elsewhere.
