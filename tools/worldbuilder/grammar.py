#!/usr/bin/env python3
"""Description Grammar Engine — Tracery-style procedural text generation.

Supports:
    ${rule}         — expand a grammar rule (random selection from list)
    ${rule.a}       — with article ("a"/"an")
    ${rule.cap}     — capitalize first letter
    ${rule.s}       — pluralize (simple English)
    ${rule.upper}   — ALL CAPS
    Nested rules    — rules can reference other rules
    Seeded random   — deterministic output from seed string
    Imports         — share grammars across components

Usage as library:
    g = Grammar(rules, seed="room:3:5")
    text = g.expand("${adjective} ${tree} grows here. ${detail}")

Usage as CLI:
    grammar.py <grammar.yaml> --expand "template string" [--seed xyz]
    grammar.py <grammar.yaml> --generate N [--seed xyz]
"""

import re
import hashlib
import yaml
import argparse
from pathlib import Path


# ---------------------------------------------------------------------------
# Seeded Random
# ---------------------------------------------------------------------------

class SeededRandom:
    """Deterministic random using hash chains. Same seed = same sequence."""

    def __init__(self, seed=''):
        self.seed = seed
        self.counter = 0

    def _next_int(self):
        h = hashlib.md5(f"{self.seed}:{self.counter}".encode()).hexdigest()
        self.counter += 1
        return int(h[:8], 16)

    def choice(self, items):
        if not items:
            return ''
        return items[self._next_int() % len(items)]

    def fork(self, extra_seed):
        """Create a child RNG with an extended seed."""
        return SeededRandom(f"{self.seed}:{extra_seed}")


# ---------------------------------------------------------------------------
# Grammar Engine
# ---------------------------------------------------------------------------

# Pattern for ${rule} and ${rule.modifier}
RULE_RE = re.compile(r'\$\{(\w+)(?:\.(\w+))?\}')

# Simple English pluralization
def pluralize(word):
    if not word:
        return word
    if word.endswith('s') or word.endswith('sh') or word.endswith('ch') or word.endswith('x'):
        return word + 'es'
    if word.endswith('y') and len(word) > 1 and word[-2] not in 'aeiou':
        return word[:-1] + 'ies'
    return word + 's'


def article(word):
    """Add 'a' or 'an' before a word."""
    if not word:
        return word
    if word[0].lower() in 'aeiou':
        return 'an ' + word
    return 'a ' + word


class Grammar:
    """Tracery-style grammar with seeded random selection."""

    def __init__(self, rules=None, seed=''):
        self.rules = rules or {}  # name -> [alternatives]
        self.rng = SeededRandom(seed)
        self.max_depth = 10  # prevent infinite recursion

    def add_rules(self, rules_dict):
        """Add rules from a dictionary. Values can be strings or lists."""
        for key, value in rules_dict.items():
            if isinstance(value, list):
                self.rules[key] = [str(v) for v in value]
            else:
                self.rules[key] = [str(value)]

    def expand(self, template, depth=0):
        """Expand a template string, resolving all ${rule} references."""
        if depth > self.max_depth:
            return template

        def replacer(match):
            rule_name = match.group(1)
            modifier = match.group(2)  # None, 'a', 'cap', 's', 'upper'

            alternatives = self.rules.get(rule_name)
            if not alternatives:
                return match.group(0)  # leave unresolved

            # Pick one
            choice = self.rng.choice(alternatives)

            # Recursively expand (the choice may contain more ${rules})
            expanded = self.expand(choice, depth + 1)

            # Apply modifier
            if modifier == 'a':
                expanded = article(expanded)
            elif modifier == 'cap':
                expanded = expanded[0].upper() + expanded[1:] if expanded else ''
            elif modifier == 's':
                # Pluralize the last word
                words = expanded.rsplit(' ', 1)
                if len(words) == 2:
                    expanded = words[0] + ' ' + pluralize(words[1])
                else:
                    expanded = pluralize(expanded)
            elif modifier == 'upper':
                expanded = expanded.upper()

            return expanded

        return RULE_RE.sub(replacer, template)

    def generate(self, template, count=1, base_seed=''):
        """Generate multiple variations from a template.

        Each variation uses a different seed extension for variety.
        """
        results = []
        for i in range(count):
            self.rng = SeededRandom(f"{base_seed}:{i}")
            results.append(self.expand(template))
        return results


# ---------------------------------------------------------------------------
# Grammar File Loading
# ---------------------------------------------------------------------------

def load_grammar(path):
    """Load a grammar YAML file.

    Format:
        grammar:
          adjective: [massive, ancient, twisted, towering]
          tree: [oak, pine, birch, willow]
          detail:
            - "Birds sing in the branches."
            - "A squirrel watches from a high branch."

        templates:
          room: "A ${adjective} ${tree} grows here. ${detail}"
          short: "${adjective.cap} ${tree.s} surround you."

        imports:
          - shared_nature.yaml
    """
    path = Path(path)
    with open(path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    rules = data.get('grammar', {})
    templates = data.get('templates', {})

    # Process imports
    for imp_path in data.get('imports', []):
        imp_full = path.parent / imp_path
        if imp_full.exists():
            imp_data = load_grammar(imp_full)
            # Merge rules (imported rules don't override local ones)
            for key, val in imp_data['rules'].items():
                if key not in rules:
                    rules[key] = val

    return {'rules': rules, 'templates': templates}


# ---------------------------------------------------------------------------
# Integration with WorldBuilder specs
# ---------------------------------------------------------------------------

def expand_descriptions(spec, grammar_rules, seed_prefix=''):
    """Expand ${grammar} references in all room descriptions in a spec.

    Modifies the spec in place.
    """
    g = Grammar(seed=seed_prefix)
    g.add_rules(grammar_rules)

    for room_id, room in spec.rooms.items():
        if '${' in room.description:
            g.rng = SeededRandom(f"{seed_prefix}:{room_id}")
            room.description = g.expand(room.description)

    for thing_id, thing in spec.things.items():
        if '${' in thing.description:
            g.rng = SeededRandom(f"{seed_prefix}:{thing_id}")
            thing.description = g.expand(thing.description)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Grammar Engine — procedural text generation')
    parser.add_argument('grammar_file', help='YAML grammar file')
    parser.add_argument('--expand', help='Template string to expand')
    parser.add_argument('--template', help='Named template from the grammar file')
    parser.add_argument('--generate', type=int, default=1,
                        help='Number of variations to generate')
    parser.add_argument('--seed', default='default', help='Random seed')

    args = parser.parse_args()
    path = Path(args.grammar_file)

    if not path.exists():
        print(f"Error: {path} not found", file=sys.stderr)
        return 1

    data = load_grammar(path)
    g = Grammar(seed=args.seed)
    g.add_rules(data['rules'])

    template = args.expand
    if args.template:
        template = data['templates'].get(args.template, '')
        if not template:
            print(f"Error: template '{args.template}' not found", file=sys.stderr)
            return 1

    if not template:
        # List available templates
        print("Available templates:")
        for name, tmpl in data['templates'].items():
            print(f"  {name}: {tmpl[:60]}...")
        return 0

    results = g.generate(template, args.generate, args.seed)
    for i, text in enumerate(results):
        if args.generate > 1:
            print(f"[{i+1}] {text}")
        else:
            print(text)

    return 0


if __name__ == '__main__':
    exit(main())
