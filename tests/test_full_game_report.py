"""The whole-game report retains missing scope and never invents acceptance."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from full_game_report import make_report, report_html, source_functions, validate_inventory


class FullGameReportTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / '.deps/melee'
        (self.source / 'src/melee/ft').mkdir(parents=True)
        (self.source / 'src/melee/ft/fighter.c').write_text('/* source fixture */')
        (self.root / 'evidence.json').write_text('{"scope":"compiled only"}')
        self.data = {'schema': 'melee-web-full-game-inventory', 'version': 1,
                     'source': {'commit': 'a' * 40, 'revision': 'test original'}, 'scope': {},
                     'features': [{'id': 'fighter', 'category': 'fighters', 'title': 'Fighter',
                        'implementation': 'partial', 'acceptance': 'not_evaluated',
                        'source_refs': ['melee/ft/fighter.c'], 'depends_on': [],
                        'pass_criteria': ['Compare complete moves against original'],
                        'evidence': [], 'next_step': 'Run construction'}]}

    def test_missing_features_are_not_filtered_out(self):
        row = copy.deepcopy(self.data['features'][0])
        row.update(id='unported', implementation='not_integrated')
        self.data['features'].append(row)
        self.assertEqual(len(validate_inventory(self.data, self.root, self.source)), 2)

    def test_duplicate_missing_and_cyclic_dependencies_reject(self):
        for mode in ('duplicate', 'missing', 'cycle'):
            data = copy.deepcopy(self.data)
            if mode == 'duplicate':
                data['features'].append(copy.deepcopy(data['features'][0]))
            else:
                data['features'][0]['depends_on'] = ['missing' if mode == 'missing' else 'fighter']
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                validate_inventory(data, self.root, self.source)

    def test_missing_or_escaping_evidence_and_sources_reject(self):
        for field, value in [('evidence', '../private.json'), ('evidence', 'absent.json'),
                             ('source_refs', 'missing.c'), ('source_refs', '/etc/passwd')]:
            data = copy.deepcopy(self.data)
            data['features'][0][field] = [value]
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_inventory(data, self.root, self.source)

    def test_source_symlink_escape_rejects(self):
        outside = self.root / 'outside.c'
        outside.write_text('outside')
        (self.source / 'src/escape.c').symlink_to(outside)
        self.data['features'][0]['source_refs'] = ['escape.c']
        with self.assertRaisesRegex(ValueError, 'escaping'):
            validate_inventory(self.data, self.root, self.source)

    def test_acceptance_cannot_be_declared_without_evidence_and_integration(self):
        row = self.data['features'][0]
        row['acceptance'] = 'passed'
        with self.assertRaisesRegex(ValueError, 'acceptance requires'):
            validate_inventory(self.data, self.root, self.source)
        row['evidence'] = ['evidence.json']
        with self.assertRaisesRegex(ValueError, 'acceptance requires'):
            validate_inventory(self.data, self.root, self.source)

    def test_aliases_unknown_sizes_and_unmapped_functions_remain_visible(self):
        symbols = '\n'.join([
            'first = .text:0x100; // type:function size:0x20',
            'alias = .text:0x100; // type:function size:0x20',
            'overlap = .text:0x110; // type:function size:0x20',
            'unknown = .text:0x200; // type:function',
            'data = .data:0x300; // type:object size:0x100'])
        rows, counts = source_functions(symbols, 'melee/a.c:\n\t.text start:0x100 end:0x180')
        self.assertEqual(len(rows), 4)
        self.assertEqual(counts['unique_function_addresses'], 3)
        self.assertEqual(counts['known_function_bytes_union'], 0x30)
        self.assertEqual(counts['unknown_size_symbols'], 1)
        self.assertEqual(counts['unassigned_symbols'], 1)
        self.assertTrue(rows[1]['alias_address'])
        self.assertTrue(all(r['reference_tested'] == 'not_collected' for r in rows))

    def test_ambiguous_ownership_is_reported_not_arbitrarily_assigned(self):
        rows, counts = source_functions('f = .text:0x100; // type:function size:0x10',
            'melee/a.c:\n\t.text start:0x100 end:0x120\nmelee/b.c:\n\t.text start:0x100 end:0x110')
        self.assertEqual(rows[0]['units'], ['melee/a.c', 'melee/b.c'])
        self.assertEqual(counts['ambiguous_owner_symbols'], 1)

    def test_malformed_function_and_empty_denominator_reject(self):
        for symbols in ('unrecognized // type:function', 'data = .data:0x100; // type:object'):
            with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                source_functions(symbols, '')

    def test_declared_progress_does_not_create_linkage_or_reference_credit(self):
        row = self.data['features'][0]
        row.update(implementation='integrated', acceptance='passed', evidence=['evidence.json'])
        inventory = self.root / 'inventory.json'
        inventory.write_text(json.dumps(self.data))
        (self.root / 'dependencies.lock.json').write_text(json.dumps({'repositories': {'melee': {'commit': 'a' * 40}}}))
        config = self.source / 'config/GALE01'
        config.mkdir(parents=True)
        (config / 'symbols.txt').write_text('f = .text:0x100; // type:function size:0x10')
        (config / 'splits.txt').write_text('melee/ft/fighter.c:\n\t.text start:0x100 end:0x110')
        # Only Git identity/diff queries are isolated. No successful compiler or
        # reference comparison is simulated: neither is performed by this tool.
        with patch('full_game_report.subprocess.check_output', side_effect=['a' * 40, 'b' * 40, b'']), \
             patch('full_game_report.subprocess.run'):
            report = make_report(self.root, inventory)
        self.assertIsNone(report['coverage']['browser_linked_functions'])
        self.assertIsNone(report['coverage']['reference_tested_functions'])
        self.assertIsNone(report['coverage']['full_game_accepted_fraction'])
        self.assertEqual(report['original_source_inventory']['function_symbols'], 1)

    def test_report_data_cannot_close_its_script_element(self):
        page = report_html({'features': [{'title': '</script><script>bad()</script>'}]})
        self.assertNotIn('<script>bad()', page)
        self.assertIn('\\u003c/script>', page)


if __name__ == '__main__':
    unittest.main()
