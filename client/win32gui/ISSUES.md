# TinyMUX Win32 GUI Client — Open Issues

Updated: 2026-03-29

## Audit Notes

- `client/win32gui` shares its Hydra transport implementation with
  `client/console/src/hydra_connection.*` via
  [win32gui.vcxproj](/home/sdennis/tinymux/client/win32gui/win32gui.vcxproj).
- This audit re-verified the Win32 GUI Hydra path against the current
  `mux/proxy/hydra.proto` schema instead of assuming the older review still
  held.

## Fixed In This Pass

### Hydra `end_of_record` boundaries were ignored

- **Files:** `client/console/src/hydra_connection.cpp`,
  `client/console/src/app.cpp`, `client/win32gui/src/mainframe.cpp`,
  `client/win32gui/src/outputbuffer.cpp`
- **Issue:** The shared Hydra reader treated all `GameOutput.text` as one raw
  stream and never surfaced `GameOutput.end_of_record`. In practice, prompts
  could merge into the next server line, and the console loop would clear Hydra
  partial lines immediately because `has_partial_line()` was hardcoded false.
- **Fix:** The shared transport now tracks partial-line state and propagates an
  `end_of_record` flag to consumers. The console finalizes prompt records
  cleanly, and the Win32 GUI seals the open output line at record boundaries so
  subsequent output starts on a new line.

## Open Issues

### No current Linux-side build validation for the Visual Studio target

- **Issue:** This audit could verify the shared C++ transport and Win32 GUI
  glue statically, but it did not build the `.sln`/`.vcxproj` target or run the
  client on Windows in this session.
- **Impact:** The shared-source fix is low-risk, but final production confidence
  still needs one Windows build/run pass.

### Credentials remain plaintext in world storage

- **Issue:** The Win32 GUI world database follows the same plaintext credential
  pattern as the console client.
- **Impact:** This is acceptable for local development but weak for shared or
  managed workstation environments.
- **Fix:** Move to OS-backed secret storage or encrypted-at-rest credentials.
