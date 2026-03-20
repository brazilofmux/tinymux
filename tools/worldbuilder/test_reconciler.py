#!/usr/bin/env python3
"""Unit tests for the three-way reconciler."""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

from reconciler import (ReconcileEntry, ReconcileResult,
                        reconcile_snapshots, format_reconcile_report)


passed = 0
failed = 0


def test(name, condition):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}")


def make_room(name, description='A room.', flags=None, attrs=None, **kw):
    obj = {
        'type': 'room',
        'name': name,
        'description': description,
        'flags': sorted(flags or []),
        'attrs': attrs or {},
    }
    obj.update(kw)
    return obj


def make_exit(name, from_room, to_room, **kw):
    obj = {
        'type': 'exit',
        'name': name,
        'from_room': from_room,
        'to_room': to_room,
    }
    obj.update(kw)
    return obj


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_all_agree():
    """spec == state == live => unchanged."""
    print("test_all_agree")
    room = make_room('Hall')
    result = reconcile_snapshots(
        {'hall': room.copy()},
        {'hall': room.copy()},
        {'hall': room.copy()},
    )
    test("one entry", len(result.entries) == 1)
    test("status unchanged", result.entries[0].status == 'unchanged')
    test("no details", result.entries[0].details == [])


def test_spec_modified():
    """spec changed, live matches old state => spec_modified."""
    print("test_spec_modified")
    old = make_room('Hall', 'Old desc.')
    new_spec = make_room('Hall', 'New desc.')
    result = reconcile_snapshots(
        {'hall': new_spec},
        {'hall': old.copy()},
        {'hall': old.copy()},
    )
    test("one entry", len(result.entries) == 1)
    test("status spec_modified", result.entries[0].status == 'spec_modified')
    test("detail on description", any(d[0] == 'description' for d in result.entries[0].details))


def test_live_modified():
    """live changed, spec matches old state => live_modified."""
    print("test_live_modified")
    old = make_room('Hall', 'Original.')
    live = make_room('Hall', 'Builder edited this.')
    result = reconcile_snapshots(
        {'hall': old.copy()},
        {'hall': old.copy()},
        {'hall': live},
    )
    test("one entry", len(result.entries) == 1)
    test("status live_modified", result.entries[0].status == 'live_modified')


def test_conflict():
    """spec and live both changed differently => conflict."""
    print("test_conflict")
    old = make_room('Hall', 'Original.')
    spec = make_room('Hall', 'Spec version.')
    live = make_room('Hall', 'Builder version.')
    result = reconcile_snapshots(
        {'hall': spec},
        {'hall': old},
        {'hall': live},
    )
    test("one entry", len(result.entries) == 1)
    test("status conflict", result.entries[0].status == 'conflict')


def test_converged():
    """spec and live both changed to the same value => spec_modified (converged)."""
    print("test_converged")
    old = make_room('Hall', 'Original.')
    both = make_room('Hall', 'Same new value.')
    result = reconcile_snapshots(
        {'hall': both.copy()},
        {'hall': old},
        {'hall': both.copy()},
    )
    test("one entry", len(result.entries) == 1)
    test("status spec_modified", result.entries[0].status == 'spec_modified')
    test("converged note", any('converged' in n for n in result.entries[0].notes))


def test_missing_live():
    """Object in state but gone from live => missing_live."""
    print("test_missing_live")
    room = make_room('Hall')
    result = reconcile_snapshots(
        {'hall': room.copy()},
        {'hall': room.copy()},
        {},
    )
    test("one entry", len(result.entries) == 1)
    test("status missing_live", result.entries[0].status == 'missing_live')


def test_recycled_live():
    """dbref exists but objid doesn't match => recycled_live."""
    print("test_recycled_live")
    spec = make_room('Hall')
    state = make_room('Hall', objid='#100:111')
    live = make_room('Other Room', objid='#100:999')
    result = reconcile_snapshots(
        {'hall': spec},
        {'hall': state},
        {'hall': live},
    )
    test("one entry", len(result.entries) == 1)
    test("status recycled_live", result.entries[0].status == 'recycled_live')


def test_pending_destroy():
    """Object marked pending_destroy in state => pending_destroy."""
    print("test_pending_destroy")
    state = make_room('Hall', status='pending_destroy')
    result = reconcile_snapshots(
        {},
        {'hall': state},
        {'hall': make_room('Hall')},
    )
    test("one entry", len(result.entries) == 1)
    test("status pending_destroy", result.entries[0].status == 'pending_destroy')


def test_new_in_spec():
    """Object in spec only => spec_modified (new)."""
    print("test_new_in_spec")
    result = reconcile_snapshots(
        {'hall': make_room('Hall')},
        {},
        {},
    )
    test("one entry", len(result.entries) == 1)
    test("status spec_modified", result.entries[0].status == 'spec_modified')


def test_new_in_live():
    """Object in live only => live_modified (discovered)."""
    print("test_new_in_live")
    result = reconcile_snapshots(
        {},
        {},
        {'hall': make_room('Hall')},
    )
    test("one entry", len(result.entries) == 1)
    test("status live_modified", result.entries[0].status == 'live_modified')


def test_attr_level_diff():
    """Field-level detail: one attr changed, another unchanged."""
    print("test_attr_level_diff")
    old = make_room('Hall', attrs={'SMELL': 'roses', 'SOUND': 'birds'})
    spec = make_room('Hall', attrs={'SMELL': 'smoke', 'SOUND': 'birds'})
    result = reconcile_snapshots(
        {'hall': spec},
        {'hall': old.copy()},
        {'hall': old.copy()},
    )
    test("status spec_modified", result.entries[0].status == 'spec_modified')
    detail_fields = [d[0] for d in result.entries[0].details]
    test("attrs detail present", 'attrs' in detail_fields)


def test_flag_change():
    """Flag added in spec, live unchanged => spec_modified."""
    print("test_flag_change")
    old = make_room('Hall', flags=['FLOATING'])
    spec = make_room('Hall', flags=['FLOATING', 'SAFE'])
    result = reconcile_snapshots(
        {'hall': spec},
        {'hall': old.copy()},
        {'hall': old.copy()},
    )
    test("status spec_modified", result.entries[0].status == 'spec_modified')


def test_multiple_objects():
    """Multiple objects with mixed statuses."""
    print("test_multiple_objects")
    result = reconcile_snapshots(
        {
            'hall': make_room('Hall', 'Same.'),
            'garden': make_room('Garden', 'New garden.'),
        },
        {
            'hall': make_room('Hall', 'Same.'),
            'garden': make_room('Garden', 'Old garden.'),
            'cellar': make_room('Cellar'),
        },
        {
            'hall': make_room('Hall', 'Same.'),
            'garden': make_room('Garden', 'Old garden.'),
        },
    )
    statuses = {e.obj_id: e.status for e in result.entries}
    test("hall unchanged", statuses.get('hall') == 'unchanged')
    test("garden spec_modified", statuses.get('garden') == 'spec_modified')
    test("cellar missing_live", statuses.get('cellar') == 'missing_live')


def test_summary():
    """Summary counts are correct."""
    print("test_summary")
    result = reconcile_snapshots(
        {'a': make_room('A'), 'b': make_room('B', 'New.')},
        {'a': make_room('A'), 'b': make_room('B', 'Old.')},
        {'a': make_room('A'), 'b': make_room('B', 'Old.')},
    )
    test("summary has unchanged", result.summary.get('unchanged') == 1)
    test("summary has spec_modified", result.summary.get('spec_modified') == 1)


def test_format_report():
    """format_reconcile_report produces non-empty output."""
    print("test_format_report")
    result = reconcile_snapshots(
        {'hall': make_room('Hall', 'New.')},
        {'hall': make_room('Hall', 'Old.')},
        {'hall': make_room('Hall', 'Old.')},
    )
    report = format_reconcile_report(result)
    test("report not empty", len(report) > 0)
    test("contains SPEC_MODIFIED", 'SPEC_MODIFIED' in report)
    test("contains Summary", 'Summary' in report)


def test_empty():
    """No objects at all => empty result."""
    print("test_empty")
    result = reconcile_snapshots({}, {}, {})
    test("no entries", len(result.entries) == 0)
    report = format_reconcile_report(result)
    test("report says no objects", 'No managed objects' in report)


# ---------------------------------------------------------------------------

if __name__ == '__main__':
    test_all_agree()
    test_spec_modified()
    test_live_modified()
    test_conflict()
    test_converged()
    test_missing_live()
    test_recycled_live()
    test_pending_destroy()
    test_new_in_spec()
    test_new_in_live()
    test_attr_level_diff()
    test_flag_change()
    test_multiple_objects()
    test_summary()
    test_format_report()
    test_empty()

    total = passed + failed
    print(f"\n{passed}/{total} passed", end="")
    if failed:
        print(f", {failed} FAILED")
        sys.exit(1)
    else:
        print(" — all reconciler tests pass")
