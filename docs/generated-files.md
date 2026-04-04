# Generated Files

Do not hand-edit generated files in this repository. Edit the source input and regenerate the derived output instead.

## Review Rule
- If a generated file changes, the diff should also include the source input or the regeneration step that justifies that output change.
- If only the generated output changed, treat that as suspicious until proven otherwise.

## Generated File Map

| Generated file | Edit this instead | Regenerate with |
|---|---|---|
| `mux/modules/engine/art_scan.cpp` | `mux/modules/engine/art_scan.rl` | `ragel -G2` |
| `mux/modules/engine/ast_scan.cpp` | `mux/modules/engine/ast_scan.rl` | `ragel -G2` |
| `mux/lib/color_ops.c` | `mux/lib/color_ops.rl` | `ragel -G2 -C` |
| `mux/muxescape/muxescape.cpp` | `mux/muxescape/muxescape.rl` | `ragel -G2` |
| `mux/include/utf8tables.h` | Unicode inputs under `utf/` | `utf/` pipeline / `make` |
| `mux/lib/utf8tables.cpp` | Unicode inputs under `utf/` | `utf/` pipeline / `make` |
| `mux/include/unicode_tables_c.h` | Unicode inputs under `utf/` | `utf/` pipeline / `make` |
| `mux/lib/unicode_tables.c` | Unicode inputs under `utf/` | `utf/` pipeline / `make` |
| `mux/include/ducet_cetable.h` | `utf/allkeys.txt` | `utf/gen_ducet.pl` |
| `mux/rv64/src/unicode_tables.c` | Unicode inputs under `utf/` | `utf/` pipeline / `make` |
| `mux/configure` | `mux/configure.ac` | `autoconf` |
| `mux/aclocal.m4` | Autoconf inputs | `aclocal` / autoconf toolchain |
| `client/*/src/hydra.pb.h` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/hydra.pb.cc` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/hydra.grpc.pb.h` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/hydra.grpc.pb.cc` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/proto/hydra.pb.h` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/proto/hydra.pb.cc` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/proto/hydra.grpc.pb.h` | `mux/proxy/hydra.proto` | `protoc` |
| `client/*/src/proto/hydra.grpc.pb.cc` | `mux/proxy/hydra.proto` | `protoc` |
| `mux/sqlite/sqlite3.c` | SQLite upstream source | replace generated amalgamation |
| `mux/sqlite/sqlite3.h` | SQLite upstream source | replace generated amalgamation |

## Notes
- Generated files may also be marked read-only on disk or checked by hooks, but those safeguards do not replace review discipline.
- When in doubt, search for the generator before editing the output.
