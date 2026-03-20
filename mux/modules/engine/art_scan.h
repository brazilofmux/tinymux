/*! \file art_scan.h
 * \brief Interface for the Ragel-generated English article scanner.
 */

#ifndef ART_SCAN_H
#define ART_SCAN_H

#include "config.h"

// Returns true if the lowercased word should use "an", false for "a".
//
bool art_should_use_an(const UTF8 *data, size_t len);

#endif // ART_SCAN_H
