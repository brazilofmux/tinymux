# Docker — Open Issues

Updated: 2026-03-27

## Critical — Outdated Images

### Dockerfile references MUX 2.12, current is 2.14

- **File:** `tinymux/Dockerfile:3-4, 21-24`
- **Issue:** Hard-coded references to `mux-2.12.0.10.unix.tar.gz` and `mux2.12` directory. The AnonymousMUX Dockerfile also references `tinymux:2.12.0.10`.
- **Impact:** Dockerfiles are non-functional with the current codebase. Building produces a container running code that is years behind.
- **Fix:** Update to 2.14.x paths, or better, build from source within the container.

## Medium — Build Configuration

### Alpine + clang is unusual and undocumented

- **File:** `tinymux/Dockerfile:2, 6, 9`
- **Issue:** Uses Alpine Linux with clang. No documentation of why clang is preferred over gcc. C++ standard level is not specified.
- **Risk:** Subtle behavior differences between clang and the gcc that developers typically use.

### No multi-stage build

- **Issue:** Build tools (clang, make, etc.) remain in the final image, increasing its size and attack surface.
- **Opportunity:** Use a multi-stage Dockerfile — build in one stage, copy binaries to a minimal runtime stage.
