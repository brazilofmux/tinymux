#!/usr/bin/env python3
"""WorldBuilder Test Harness — automated tests for the tool suite.

Tests all offline functionality (no live MUX needed):
- Spec parsing
- DRC validation (positive and negative)
- Component expansion
- Procedural grid generation
- Compiler output
- Diff engine
- Softcode linting
- Grammar engine
- Project management
- Migration system
- Map generation
- MUX text escaping

Run: python test_worldbuilder.py
"""

import sys
import os
import ssl

# Ensure we can import from the worldbuilder directory and that fixture
# paths resolve correctly regardless of the caller's working directory.
_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _here)
os.chdir(_here)

from worldbuilder import (parse_spec, check_drc, compile_spec, format_commands,
                          format_plan, mux_escape, mux_unescape, diff_spec,
                          compile_incremental)
from softcode_lint import lint_softcode, lint_spec
from grammar import Grammar, SeededRandom
from mapgen import extract_graph, layout_grid, render_ascii, render_dot
from reconciler import build_spec_snapshot, reconcile_snapshots
from executor import MuxConnection
import executor as executor_module


passed = 0
failed = 0


def test(name, condition):
    global passed, failed
    if condition:
        passed += 1
        print(f"  PASS  {name}")
    else:
        failed += 1
        print(f"  FAIL  {name}")


def section(name):
    print(f"\n=== {name} ===")


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_mux_escape():
    section("MUX Text Escaping")
    test("Newlines become %r", '%r' in mux_escape("line1\nline2"))
    test("Literal % becomes %%", '%%' in mux_escape("100% complete"))
    test("No double-escape", '%%%%' not in mux_escape("100% complete"))
    test("Trailing whitespace stripped", not mux_escape("hello  \n").endswith('  '))
    test("Round-trip", mux_unescape(mux_escape("hello\nworld").replace('%%', '%')) == "hello\nworld")


def test_spec_parsing():
    section("Spec Parsing")
    spec = parse_spec("tests/park.yaml")
    test("Zone name", spec.zone.name == "Emerald Park")
    test("3 rooms", len(spec.rooms) == 3)
    test("2 exits", len(spec.exits) == 2)
    test("Room has description", len(spec.rooms['park_entrance'].description) > 20)
    test("Exit has back", spec.exits[0].back_name is not None)


def test_drc_positive():
    section("DRC — Valid Specs")
    for name in ['park', 'apartments', 'forest', 'town']:
        spec = parse_spec(f"tests/{name}.yaml")
        drc = check_drc(spec)
        test(f"{name}.yaml passes DRC", drc.ok)


def test_drc_negative():
    section("DRC — Invalid Spec")
    spec = parse_spec("tests/bad_park.yaml")
    drc = check_drc(spec)
    test("Has errors", not drc.ok)
    test("Catches empty description", any('no description' in e.lower() for e in drc.errors))
    test("Catches WIZARD flag", any('WIZARD' in e for e in drc.errors))
    test("Catches ADESTROY attr", any('ADESTROY' in e for e in drc.errors))
    test("Catches dangling exit", any('unknown room' in e.lower() for e in drc.errors))
    test("Catches alias conflict", any('alias conflict' in e.lower() for e in drc.errors))
    test("Catches unreachable", any('unreachable' in e.lower() for e in drc.errors))


def test_components():
    section("Component Expansion")
    spec = parse_spec("tests/apartments.yaml")
    test("8 rooms expanded", len(spec.rooms) == 8)
    test("Apartment 201 exists", 'apt_201_living_room' in spec.rooms)
    test("Apartment 201 name", 'Apartment 201' in spec.rooms['apt_201_living_room'].name)
    test("Rent attribute", spec.rooms['apt_201_living_room'].attrs.get('RENT_AMOUNT') == '150')


def test_procedural_grid():
    section("Procedural Grid Generation")
    spec = parse_spec("tests/forest.yaml")
    test("17 rooms (16 grid + 1 edge)", len(spec.rooms) == 17)
    test("Has grid room 0,0", 'darkwood_0_0' in spec.rooms)
    test("Has grid room 3,3", 'darkwood_3_3' in spec.rooms)
    test("Edge room exists", 'forest_edge' in spec.rooms)
    test("Multiple exits", len(spec.exits) > 10)


def test_things():
    section("Things (Objects)")
    spec = parse_spec("tests/town.yaml")
    test("Has things", len(spec.things) > 0)
    test("Well exists", 'well' in spec.things)
    test("Barkeeper exists", 'tavern_barkeeper' in spec.things)
    test("Barkeeper name", 'Old Martha' in spec.things['tavern_barkeeper'].name)


def test_compiler():
    section("Compiler Output")
    spec = parse_spec("tests/park.yaml")
    commands = compile_spec(spec)
    cmd_strs = [c for _, c in commands if c]
    test("Has @dig commands", any('@dig' in c for c in cmd_strs))
    test("Has @desc commands", any('@desc' in c for c in cmd_strs))
    test("Has @open commands", any('@open' in c for c in cmd_strs))
    test("Has @chzone commands", any('@chzone' in c for c in cmd_strs))

    # Format tests
    default = format_commands(commands, 'default')
    test("Default has comments", '#' in default)

    raw = format_commands(commands, 'raw')
    test("Raw has no comments", '# ' not in raw)

    upload = format_commands(commands, 'upload')
    test("Upload has terminators", '\n-\n' in upload)


def test_softcode_lint():
    section("Softcode Linting")
    # Good code
    r = lint_softcode("[pemit(%#,Hello)]", "test")
    test("Good code passes", r.ok)

    # Bad brackets
    r = lint_softcode("[pemit(%#,Hello", "test")
    test("Unclosed bracket caught", not r.ok)

    # Unknown function
    r = lint_softcode("[bogus(1)]", "test")
    test("Unknown function warned", len(r.warnings) > 0)

    # Dangerous
    r = lint_softcode("@force %#=bad", "test")
    test("@force caught", not r.ok)

    r = lint_softcode("@pemit %#=secret", "test")
    test("@pemit %# caught", not r.ok)

    r = lint_softcode("@trigger thing/attr", "test")
    test("@trigger caught", not r.ok)

    r = lint_softcode("%(unsafe)", "test")
    test("%(...) caught", not r.ok)

    r = lint_softcode("[setr(0,%0)]", "test")
    test("setr() caught", not r.ok)

    r = lint_softcode("[mailcount(me)]", "test")
    test("mail*() caught", not r.ok)


def test_mux_connection_tls():
    section("Executor TLS")

    class FakeSocket:
        def __init__(self):
            self.timeouts = []

        def settimeout(self, timeout):
            self.timeouts.append(timeout)

        def recv(self, _size):
            raise executor_module.socket.timeout

    class FakeWrappedSocket(FakeSocket):
        pass

    class FakeContext:
        def __init__(self):
            self.check_hostname = True
            self.verify_mode = ssl.CERT_REQUIRED
            self.wrap_calls = []

        def wrap_socket(self, raw, server_hostname=None):
            self.wrap_calls.append((raw, server_hostname))
            return FakeWrappedSocket()

    original_create_connection = executor_module.socket.create_connection
    original_create_default_context = executor_module.ssl.create_default_context
    created_contexts = []

    def fake_create_connection(_addr, _timeout):
        return FakeSocket()

    def fake_create_default_context():
        ctx = FakeContext()
        created_contexts.append(ctx)
        return ctx

    executor_module.socket.create_connection = fake_create_connection
    executor_module.ssl.create_default_context = fake_create_default_context
    try:
        conn = MuxConnection("mux.example", 4201, use_ssl=True)
        conn.connect()
        secure_ctx = created_contexts[-1]
        test("TLS verifies by default", secure_ctx.verify_mode == ssl.CERT_REQUIRED)
        test("TLS checks hostname by default", secure_ctx.check_hostname)

        conn = MuxConnection("mux.example", 4201, use_ssl=True, verify_ssl=False)
        conn.connect()
        insecure_ctx = created_contexts[-1]
        test("Insecure mode disables verification", insecure_ctx.verify_mode == ssl.CERT_NONE)
        test("Insecure mode disables hostname checks", not insecure_ctx.check_hostname)
    finally:
        executor_module.socket.create_connection = original_create_connection
        executor_module.ssl.create_default_context = original_create_default_context


def test_grammar():
    section("Grammar Engine")
    g = Grammar({'color': ['red', 'blue', 'green'], 'size': ['big', 'small']}, seed='test')
    result = g.expand("A ${size} ${color} ball.")
    test("Grammar expands", '${' not in result)
    test("Contains words", len(result) > 10)

    # Determinism
    g1 = Grammar({'x': ['a', 'b', 'c']}, seed='same')
    g2 = Grammar({'x': ['a', 'b', 'c']}, seed='same')
    test("Same seed = same result", g1.expand("${x}") == g2.expand("${x}"))

    # Different seeds
    g3 = Grammar({'x': ['a', 'b', 'c']}, seed='different')
    # Might be same by chance but unlikely over multiple
    results = set()
    for i in range(10):
        g_t = Grammar({'x': ['alpha', 'beta', 'gamma', 'delta']}, seed=f'seed{i}')
        results.add(g_t.expand("${x}"))
    test("Different seeds give variety", len(results) > 1)

    # Modifiers
    g = Grammar({'thing': ['apple']}, seed='mod')
    test("Article modifier", g.expand("${thing.a}") == "an apple")
    g.rng = SeededRandom('mod2')
    test("Cap modifier", g.expand("${thing.cap}") == "Apple")


def test_map_generation():
    section("Map Generation")
    spec = parse_spec("tests/park.yaml")
    nodes, edges = extract_graph(spec)
    test("3 nodes", len(nodes) == 3)
    test("2 edges", len(edges) == 2)

    positions = layout_grid(nodes, edges)
    test("All positioned", len(positions) == 3)

    ascii_map = render_ascii(nodes, edges, positions)
    test("ASCII map has content", len(ascii_map) > 50)
    test("ASCII map has boxes", '+' in ascii_map)

    dot = render_dot(nodes, edges)
    test("DOT has digraph", 'digraph' in dot)
    test("DOT has nodes", 'park_entrance' in dot)


def test_exit_model():
    section("Extended Exit Model")
    spec = parse_spec("tests/park.yaml")
    # Basic exits don't have lock/succ/etc — that's fine
    test("Exit has from_room", spec.exits[0].from_room == 'park_entrance')
    test("Exit lock is None", spec.exits[0].lock is None)
    test("Exit flags empty", spec.exits[0].flags == [])


def test_reconciliation():
    section("Reconciliation")
    spec = parse_spec("tests/park.yaml")
    spec_objects = build_spec_snapshot(spec)
    state_objects = build_spec_snapshot(spec)
    for obj_id, obj in state_objects.items():
        obj['dbref'] = f'#{1000 + len(obj_id)}'
        obj['objid'] = f'{obj["dbref"]}:1'

    live_objects = {}
    for obj_id, obj in state_objects.items():
        live_objects[obj_id] = dict(obj)

    live_objects['park_entrance']['description'] = 'Hand edited live description.'
    spec_objects['fountain_plaza']['attrs']['SOUND'] = 'Water splashes softly.'

    result = reconcile_snapshots(spec_objects, state_objects, live_objects)
    statuses = {entry.obj_id: entry.status for entry in result.entries}
    test("Live modification detected", statuses['park_entrance'] == 'live_modified')
    test("Spec modification detected", statuses['fountain_plaza'] == 'spec_modified')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("WorldBuilder Test Harness")
    print("=" * 40)

    test_mux_escape()
    test_spec_parsing()
    test_drc_positive()
    test_drc_negative()
    test_components()
    test_procedural_grid()
    test_things()
    test_compiler()
    test_softcode_lint()
    test_grammar()
    test_map_generation()
    test_exit_model()
    test_reconciliation()
    test_mux_connection_tls()

    print(f"\n{'=' * 40}")
    print(f"Results: {passed} passed, {failed} failed")

    return 0 if failed == 0 else 1


if __name__ == '__main__':
    exit(main())
