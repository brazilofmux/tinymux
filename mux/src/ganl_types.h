#ifndef GANL_TYPES_H
#define GANL_TYPES_H

// This file provides a compatibility layer for using GANL within TinyMUX
// It includes the original GANL types and adds any necessary bridging code

#include "autoconf.h"
#include "config.h"  // For UTF8 and other TinyMUX types
#include "timeutil.h"

// TinyMUX defines ASCII as a macro, which conflicts with the GANL EncodingType enum
// Undefine it before including GANL headers
#ifdef ASCII
#undef ASCII
#endif

// Now include the GANL types
#include "ganl/include/network_types.h"

// Compatibility types and functions
namespace ganl {
    // Define a timestamp conversion utility
    inline time_t timeFromCLinearTimeAbsolute(const CLinearTimeAbsolute& t) {
        // Convert to a standardized time format
        return time_t(t.ReturnSeconds());
    }
}

#endif // GANL_TYPES_H