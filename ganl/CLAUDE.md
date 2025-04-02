# GANL (Networking Library) Guide

## Build Commands
```bash
./autogen.sh                # Generate configure script
./configure                 # Configure build
make                        # Build entire project
make check                  # Run all tests
cd test/unit && make check  # Run unit tests only
cd test/integration && make check # Run integration tests only
```

## Documentation
- **Architecture Specification**: See `docs/networkspec.md` for complete architectural details
- **Interface Definitions**: Header files in the `include/ganl/` directory

## Code Style
- **Indentation**: 4 spaces (no tabs)
- **Line width**: 100-120 characters max
- **Naming**:
  - Classes: PascalCase (EchoSessionManager)
  - Methods/Functions: camelCase (processSecureData)
  - Variables: camelCase with trailing underscore for members (state_)
  - Constants/Macros: UPPER_SNAKE_CASE
- **Includes**: Group in order: 1) Project headers 2) System headers
- **Headers**: Use include guards (#ifndef GANL_FILE_H)
- **Error handling**: Error codes or exceptions with detailed messages
- **Debug**: Use debug macros (GANL_CONN_DEBUG) for diagnostics, disabled in production
- **Documentation**: Document public interfaces with detailed comments