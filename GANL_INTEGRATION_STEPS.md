# GANL Integration Steps

This document outlines the process for flattening the GANL structure and integrating it into the TinyMUX build system. The goal is to move all GANL files into `mux/src` while preserving functionality.

## File Reorganization Plan

### 1. Mapping GANL files to TinyMUX naming structure

| GANL File | Target TinyMUX File | Notes |
|-----------|---------------------|-------|
| `include/ganl/network_types.h` | `mux/src/ganl_types.h` | Core type definitions |
| `include/ganl/io_buffer.h` | `mux/src/ganl_buffer.h` | Buffer manipulation |
| `include/ganl/connection.h` | `mux/src/ganl_connection.h` | Connection base class |
| `include/ganl/network_engine.h` | `mux/src/ganl_engine.h` | Network engine interface |
| `include/ganl/protocol_handler.h` | `mux/src/ganl_protocol.h` | Protocol handling |
| `include/ganl/secure_transport.h` | `mux/src/ganl_ssl.h` | SSL/TLS handling |
| `include/ganl/session_manager.h` | `mux/src/ganl_session.h` | Session management |
| `src/common/io_buffer.cpp` | `mux/src/ganl_buffer.cpp` | Buffer implementation |
| `src/common/connection.cpp` | `mux/src/ganl_connection.cpp` | Connection implementation |
| `src/platform/factory/network_engine_factory.cpp` | `mux/src/ganl_engine_factory.cpp` | Engine factory |
| `src/platform/factory/network_engine_factory.h` | (inline in `ganl_engine_factory.cpp`) | Factory header |
| `src/protocol/telnet/telnet_protocol_handler.cpp` | `mux/src/ganl_telnet.cpp` | Telnet protocol |
| `src/protocol/telnet/telnet_protocol_handler.h` | (inline in `ganl_telnet.cpp`) | Telnet header |
| `src/ssl/factory/secure_transport_factory.cpp` | `mux/src/ganl_ssl_factory.cpp` | SSL factory |
| `src/ssl/factory/secure_transport_factory.h` | (inline in `ganl_ssl_factory.cpp`) | SSL factory header |
| `src/ssl/openssl/openssl_transport.cpp` | `mux/src/ganl_openssl.cpp` | OpenSSL implementation |
| `src/ssl/openssl/openssl_transport.h` | (inline in `ganl_openssl.cpp`) | OpenSSL header |

### 2. Platform-specific implementations

Each platform will have dedicated files that can be conditionally compiled:

#### UNIX (epoll, kqueue, select)
| GANL File | Target TinyMUX File | Notes |
|-----------|---------------------|-------|
| `src/platform/epoll/epoll_network_engine.cpp` + `.h` | `mux/src/ganl_epoll.cpp` | Linux platform |
| `src/platform/kqueue/kqueue_network_engine.cpp` + `.h` | `mux/src/ganl_kqueue.cpp` | BSD platform |
| `src/platform/select/select_network_engine.cpp` + `.h` | `mux/src/ganl_select.cpp` | Fallback Unix platform |

#### Windows (IOCP, wselect)
| GANL File | Target TinyMUX File | Notes |
|-----------|---------------------|-------|
| `src/platform/iocp/iocp_network_engine.cpp` + `.h` | `mux/src/ganl_iocp.cpp` | Windows IOCP |
| `src/platform/wselect/wselect_network_engine.cpp` + `.h` | `mux/src/ganl_wselect.cpp` | Windows select fallback |
| `src/ssl/schannel/schannel_transport.cpp` + `.h` | `mux/src/ganl_schannel.cpp` | Windows SSL |

## Implementation Process

### 1. Directory Structure

Create a new section in the TinyMUX Makefile for GANL components:

```makefile
# GANL Networking Components
GANL_OBJS = ganl_types.o ganl_buffer.o ganl_connection.o ganl_engine.o ganl_protocol.o \
            ganl_session.o ganl_telnet.o ganl_ssl_factory.o
```

### 2. Include Path Update

Modify all GANL source files to update include paths:

From:
```cpp
#include <ganl/network_types.h>
#include <ganl/io_buffer.h>
```

To:
```cpp
#include "ganl_types.h"
#include "ganl_buffer.h"
```

### 3. Platform-specific Components

Add platform detection in TinyMUX configure.ac to determine which network engine to use:

```
# Platform-specific GANL components
if test "$OSTYPE" = "linux-gnu"; then
    GANL_PLATFORM_OBJS="ganl_epoll.o"
    AC_DEFINE(HAVE_EPOLL, 1, [Define if epoll is available])
elif test "$OSTYPE" = "freebsd"; then
    GANL_PLATFORM_OBJS="ganl_kqueue.o"
    AC_DEFINE(HAVE_KQUEUE, 1, [Define if kqueue is available])
else
    GANL_PLATFORM_OBJS="ganl_select.o"
fi
```

### 4. SSL Implementation

Add conditional compilation for SSL backends:

```
# SSL Implementation
if test "$OSTYPE" = "windows"; then
    GANL_SSL_OBJS="ganl_schannel.o"
    AC_DEFINE(USE_SCHANNEL, 1, [Define to use SChannel for SSL])
else
    GANL_SSL_OBJS="ganl_openssl.o"
    AC_DEFINE(USE_OPENSSL, 1, [Define to use OpenSSL for SSL])
fi
```

### 5. Build System Integration

Update TinyMUX Makefile to include the GANL objects in the build:

```makefile
NETMUX_OBJS = ... existing objects ... \
              $(GANL_OBJS) $(GANL_PLATFORM_OBJS) $(GANL_SSL_OBJS)
```

### 6. Implementation Class

Create a new implementation of the SessionManager interface that bridges to TinyMUX:

```cpp
// mux/src/ganl_mux_session.h
class MuxSessionManager : public ganl::SessionManager {
    // Implementation that bridges to TinyMUX's player handling
};

// mux/src/ganl_mux_session.cpp
// Implementation details
```

### 7. Integration Point

Create a new file to serve as the main integration point between GANL and TinyMUX:

```cpp
// mux/src/ganl_main.cpp
// Initialization and shutdown functions
bool initialize_ganl_networking();
void shutdown_ganl_networking();
void ganl_process_events(int timeout_ms);
```

### 8. Feature Flags

Add a configuration option to enable/disable GANL networking:

```
# Configure options
AC_ARG_ENABLE(ganl,
    [  --enable-ganl           Use GANL networking system (experimental)],
    [use_ganl=$enableval],
    [use_ganl=no])

if test "$use_ganl" = "yes"; then
    AC_DEFINE(USE_GANL, 1, [Define to use GANL networking])
fi
```

## Migration Steps

1. Copy all GANL files to their new locations in mux/src
2. Update include paths and namespace references
3. Create TinyMUX-specific implementations of interfaces
4. Add build system modifications
5. Create integration points
6. Implement feature flags for migration
7. Test compiling and linking
8. Implement side-by-side mode (both networking systems simultaneously)
9. Create migration tools and documentation

## Testing Strategy

1. Build with GANL disabled (traditional TinyMUX)
2. Build with GANL enabled in side-by-side mode
3. Test connection handling with the new system
4. Verify SSL functionality
5. Test telnet negotiation
6. Test under load