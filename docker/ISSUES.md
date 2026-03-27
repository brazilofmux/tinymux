# Docker — Open Issues

Updated: 2026-03-27

## ~~Critical — Outdated Images~~ FIXED

### ~~Dockerfile references MUX 2.12~~ FIXED

- Synced from `~/g/tinymux/Dockerfile` which targets MUX 2.13.0.11 with multi-stage build, pcre2, sqlite, and proper module installation. AnonymousMUX base image tag updated to match.

## ~~Medium — Build Configuration~~ FIXED

### ~~No multi-stage build~~ FIXED

- The current Dockerfile already uses multi-stage build (builder stage with build tools, runtime stage with only `libstdc++`, `libc6-compat`, `openssl`, `pcre2`, `sqlite`).
