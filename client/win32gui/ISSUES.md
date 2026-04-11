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

### ~~No current Linux-side build validation for the Visual Studio target~~ FIXED

- Added `client/win32gui/validate_vcxproj.py`, a Linux-side validator for the
  Visual Studio project. It parses `win32gui.vcxproj`, verifies that all
  referenced compile/include/resource paths exist, checks that the expected
  `Debug|x64` and `Release|x64` configurations are present, and asserts that
  the key shared Hydra/console source files remain wired into the project.
  Reverified with `python3 client/win32gui/validate_vcxproj.py`.
- This does not replace a real Windows build/run pass, but it closes the
  specific "no Linux-side validation at all" gap that the tracker called out.

### ~~Credentials remain plaintext in world storage~~ FIXED

- Hydra passwords moved from plaintext `worlds.json` to Windows Credential Manager via `CredWriteW`/`CredReadW` (`CRED_TYPE_GENERIC`, `CRED_PERSIST_LOCAL_MACHINE`). Existing JSON passwords are transparently migrated to the credential store on first load and stripped from JSON on next save.
