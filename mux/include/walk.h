/*! \file walk.h
 * \brief NPC movement primitives: @walk and @patrol.
 *
 * Server-side commands that move objects one hop per tick along routed
 * paths, consuming routing data without softcode overhead.
 * See docs/design-routing.md Phase 4.
 */

#ifndef WALK_H
#define WALK_H

#include "copyright.h"

// Switch flags for @walk/@patrol.
//
constexpr int WALK_STOP   = 0x01;
constexpr int WALK_QUIET  = 0x02;
constexpr int WALK_LOCKED = 0x04;

// Initialize/shutdown the walk subsystem.
//
void walk_init(void);
void walk_shutdown(void);

// Cancel all walk/patrol entries for a given object.
//
void walk_clr(dbref npc);

// Command handlers.
//
void do_walk(dbref executor, dbref caller, dbref enactor, int eval,
             int key, int nargs, UTF8 *what, UTF8 *where,
             const UTF8 *cargs[], int ncargs);

void do_patrol(dbref executor, dbref caller, dbref enactor, int eval,
               int key, int nargs, UTF8 *what, UTF8 *waypoints,
               const UTF8 *cargs[], int ncargs);

#endif // !WALK_H
