#!/usr/bin/env python3
"""Test Attribute-Level Diffing (WorldBuilder v3)."""

import sys
import os
import yaml
from pathlib import Path

# Ensure we can import from the worldbuilder directory
sys.path.insert(0, os.path.dirname(__file__))

from worldbuilder import parse_spec, diff_spec, compile_incremental, format_diff, Room, WorldSpec, Zone, Change

def test_attribute_level_diff():
    print("Testing Attribute-Level Diffing...")
    
    # 1. Mock a state file
    state_dir = Path(".worldbuilder_test")
    state_dir.mkdir(exist_ok=True)
    state_path = state_dir / "test_diff.state.yaml"
    
    state_data = {
        'state_version': 3,
        'zone': 'Test Zone',
        'objects': {
            'room1': {
                'dbref': '#100',
                'type': 'room',
                'name': 'Old Name',
                'description': 'Old description.',
                'flags': ['FLOATING'],
                'attrs': {
                    'SMELL': 'Old smell',
                    'SOUND': 'Old sound'
                }
            }
        }
    }
    with open(state_path, 'w') as f:
        yaml.dump(state_data, f)
    
    # 2. Create a spec with changes
    room1 = Room('room1', 'New Name', 'New description.', 
                 flags=['SAFE'], # FLOATING removed, SAFE added
                 attrs={
                     'SMELL': 'New smell', # Modified
                     # SOUND removed
                     'VISION': 'New vision' # Added
                 })
    spec = WorldSpec()
    spec.zone = Zone('Test Zone')
    spec.rooms = {'room1': room1}
    spec.exits = []
    
    # 3. Perform diff
    changes = diff_spec(spec, state_path)
    
    print("\nDiff Result:")
    print(format_diff(changes))
    
    # Assertions
    assert len(changes) == 1
    ch = changes[0]
    assert ch.action == Change.MODIFY
    
    details = {field: (old, new) for field, old, new in ch.details}
    assert 'name' in details
    assert details['name'] == ('Old Name', 'New Name')
    assert 'description' in details
    assert 'flags+' in details
    assert 'flags-' in details
    assert 'attr:SMELL' in details
    assert 'attr:SOUND-' in details
    assert 'attr:VISION+' in details
    
    print("\nAssertions passed for diff_spec.")
    
    # 4. Test incremental compile
    commands = compile_incremental(spec, state_path)
    print("\nIncremental Compile Output:")
    for comment, cmd in commands:
        if cmd:
            print(f"  {cmd}")
        elif comment:
            print(f"  # {comment}")
            
    # Check for specific commands
    cmd_texts = [c for _, c in commands if c]
    assert '@name here=New Name' in cmd_texts
    assert '@desc here=New description.' in cmd_texts
    assert '@set here=SAFE' in cmd_texts
    assert '@set here=!FLOATING' in cmd_texts
    assert '&SMELL here=New smell' in cmd_texts
    assert '&VISION here=New vision' in cmd_texts
    assert '&SOUND here=' in cmd_texts
    
    print("\nAssertions passed for compile_incremental.")
    
    # 5. Test deletion
    spec_empty = WorldSpec()
    spec_empty.zone = Zone('Test Zone')
    spec_empty.rooms = {}
    spec_empty.exits = []
    
    commands_del = compile_incremental(spec_empty, state_path)
    print("\nIncremental Compile (Deletion) Output:")
    for comment, cmd in commands_del:
        if cmd:
            print(f"  {cmd}")
        elif comment:
            print(f"  # {comment}")
            
    cmd_texts_del = [c for _, c in commands_del if c]
    assert any('@destroy/override #100' in c for c in cmd_texts_del)
    print("\nAssertions passed for deletion.")
    
    # Cleanup
    state_path.unlink()
    state_dir.rmdir()

if __name__ == '__main__':
    try:
        test_attribute_level_diff()
        print("\nALL V3 DIFF TESTS PASSED")
    except Exception as e:
        print(f"\nTEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
