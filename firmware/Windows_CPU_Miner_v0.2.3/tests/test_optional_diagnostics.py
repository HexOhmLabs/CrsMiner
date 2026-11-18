"""v0.2.2: optional log, preservation of DIAG2 settings and safe cleanup."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from config_and_hardware import default_settings, load_settings, save_settings
from diagnostic_log import (DiagnosticLog, clear_diagnostic_csv_files,
                            diagnostic_csv_files, diagnostics_directory)


class OptionalDiagnosticsTests(unittest.TestCase):
    def test_diagnostics_default_on_and_setting_persists(self):
        with tempfile.TemporaryDirectory() as td, patch('config_and_hardware.user_data_dir', return_value=Path(td)):
            cfg = default_settings()
            self.assertTrue(cfg['diagnostics_enabled'])
            cfg['diagnostics_enabled'] = False
            save_settings(cfg)
            loaded, _ = load_settings()
            self.assertFalse(loaded['diagnostics_enabled'])
            self.assertEqual(json.loads((Path(td)/'config_v023.json').read_text())['diagnostics_enabled'], False)

    def test_imports_diag2_without_modifying_it(self):
        with tempfile.TemporaryDirectory() as td, patch('config_and_hardware.user_data_dir', return_value=Path(td)):
            old_path = Path(td)/'config_diag2.json'
            old_cfg = default_settings()
            old_cfg.pop('diagnostics_enabled')
            old_cfg['wallet'] = 'legacy_wallet'
            old_cfg['pool'] = 'localhost:3333'
            old_path.write_text(json.dumps(old_cfg))
            before = old_path.read_bytes()
            cfg, configured = load_settings()
            self.assertTrue(configured)
            self.assertEqual(cfg['wallet'], 'legacy_wallet')
            self.assertTrue(cfg['diagnostics_enabled'])
            cfg['diagnostics_enabled'] = False
            save_settings(cfg)
            self.assertEqual(old_path.read_bytes(), before)
            self.assertTrue((Path(td)/'config_v023.json').is_file())

    def test_imports_v022_read_only_then_uses_v023(self):
        """A v0.2.2 installation remains untouched and is imported on first launch."""
        with tempfile.TemporaryDirectory() as td, patch('config_and_hardware.user_data_dir', return_value=Path(td)):
            old = Path(td) / 'config_v022.json'
            cfg = default_settings()
            cfg.update(name='pc02', wallet='DGBwallet', worker='ryzen',
                       pool='localhost:3333', diagnostics_enabled=False)
            old.write_text(json.dumps(cfg), encoding='utf-8')
            original = old.read_bytes()
            imported, configured = load_settings()
            self.assertTrue(configured)
            self.assertEqual(imported['wallet'], 'DGBwallet')
            self.assertEqual(imported['worker'], 'ryzen')
            self.assertFalse(imported['diagnostics_enabled'])
            save_settings(imported)
            self.assertEqual(old.read_bytes(), original)
            updated = dict(imported, worker='e350')
            save_settings(updated)
            current, configured = load_settings()
            self.assertTrue(configured)
            self.assertEqual(current['worker'], 'e350')
            self.assertEqual(old.read_bytes(), original)

    def test_clean_only_exact_csv_names_preserving_other_files(self):
        with tempfile.TemporaryDirectory() as td, patch('diagnostic_log.user_data_dir', return_value=Path(td)):
            folder = diagnostics_directory(create=True)
            a = folder/'CRSminer_PC_DIAG_20261118_135955_123456.csv'
            b = folder/'CRSminer_PC_DIAG_20261118_140000_654321.csv'
            a.write_text('old')
            b.write_text('old')
            unrelated = [folder/'notes.txt', folder/'report.csv',
                         folder/'CRSminer_PC_DIAG_20261118_140000_nope.csv',
                         folder/'CRSminer_PC_DIAG_20261118_140000_654321.csv.bak']
            for file in unrelated: file.write_text('keep')
            subdir = folder/'archive'; subdir.mkdir()
            (subdir/a.name).write_text('keep')
            self.assertEqual(diagnostic_csv_files(), [a, b])
            count, failures = clear_diagnostic_csv_files()
            self.assertEqual((count, failures), (2, []))
            self.assertFalse(a.exists())
            self.assertFalse(b.exists())
            for file in unrelated: self.assertEqual(file.read_text(), 'keep')
            self.assertEqual((subdir/a.name).read_text(), 'keep')

    def test_csv_logging_still_writes_when_enabled(self):
        with tempfile.TemporaryDirectory() as td, patch('diagnostic_log.user_data_dir', return_value=Path(td)):
            log = DiagnosticLog()
            log.write('session', note='start')
            path = log.path
            log.close()
            self.assertIn('session', path.read_text(encoding='utf-8-sig'))
            self.assertEqual(diagnostic_csv_files(), [path])

    def test_clear_empty_folder_creates_nothing(self):
        with tempfile.TemporaryDirectory() as td, patch('diagnostic_log.user_data_dir', return_value=Path(td)):
            self.assertEqual(diagnostic_csv_files(), [])
            self.assertEqual(clear_diagnostic_csv_files(), (0, []))
            self.assertFalse(diagnostics_directory().exists())

if __name__ == '__main__':
    unittest.main()

class DisabledMiningDiagnosticsTests(unittest.TestCase):
    def test_no_csv_or_monitor_thread_when_disabled(self):
        """Logging OFF must leave all hashing and network paths intact."""
        from miner_core import MiningEngine
        class FakeProcess:
            def start(self): pass
            def is_alive(self): return False
            def join(self, timeout=None): pass
        cfg = default_settings()
        cfg['pool'] = '127.0.0.1:3333'
        cfg['wallet'] = 'testwallet'
        cfg['diagnostics_enabled'] = False
        miner = MiningEngine(on_log=lambda _: None)
        with patch.object(miner.ctx, 'Process', return_value=FakeProcess()), \
             patch.object(miner, '_network', return_value=None), \
             patch.object(miner, '_collect', return_value=None), \
             patch('miner_core.DiagnosticLog', side_effect=AssertionError('should not create log')):
            miner.start(cfg)
            try:
                self.assertTrue(miner.running)
                self.assertIsNone(miner.diagnostic)
                self.assertIsNone(miner.diag_thread)
                self.assertTrue(miner.workers)
            finally:
                miner.stop()
