"""DIAG2 CPU assignment and safety guarantees, including job-rate persistence."""
import os
import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from config_and_hardware import (default_settings, normalize_cpu_efforts,
                                 validate_cpu_allocation, load_settings, save_settings)
from miner_core import MiningEngine


class CPUAllocationTests(unittest.TestCase):
    def test_individual_percentages_allow_full_range(self):
        cfg = {'mining_mode':'dedicated', 'cpu_efforts':[100, 75, 1, 0]}
        self.assertEqual(validate_cpu_allocation(cfg, 4), [100, 75, 1, 0])

    def test_full_cpu_saturation_forbidden(self):
        for mode in ('background', 'dedicated'):
            with self.assertRaisesRegex(ValueError, '75%'):
                validate_cpu_allocation({'mining_mode':mode, 'cpu_efforts':[100]*4}, 4)

    def test_zero_cpu_is_valid_as_reserved_cpu(self):
        cfg = {'mining_mode':'dedicated', 'cpu_efforts':[100, 100, 0]}
        self.assertEqual(validate_cpu_allocation(cfg, 3), [100, 100, 0])

    def test_all_disabled_forbidden(self):
        with self.assertRaisesRegex(ValueError, 'at least one'):
            validate_cpu_allocation({'mining_mode':'background', 'cpu_efforts':[0,0]}, 2)

    def test_invalid_modes_and_percentages(self):
        for efforts in ([-1,75], [101,0], [50.5,0], [True,0], ['100',0]):
            with self.assertRaises(ValueError):
                validate_cpu_allocation({'mining_mode':'background','cpu_efforts':efforts},2)
        with self.assertRaisesRegex(ValueError, 'Mining mode'):
            validate_cpu_allocation({'mining_mode':'real-time','cpu_efforts':[50,0]},2)

    def test_new_cpus_are_disabled_after_rediscovery(self):
        self.assertEqual(normalize_cpu_efforts([70,50],4),[70,50,0,0])
        self.assertEqual(normalize_cpu_efforts([70,50,25,0],2),[70,50])

    def test_defaults_are_safe(self):
        config = default_settings()
        efforts = validate_cpu_allocation(config)
        self.assertTrue(any(x <= 75 for x in efforts))
        self.assertTrue(any(x > 0 for x in efforts))
        self.assertEqual(config['mining_mode'], 'background')

    def test_diagnostic_settings_do_not_overwrite_stable_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            old = {'name':'pc12','wallet':'testwallet','pool':'localhost:3333',
                   'worker':'pc','threads':2,'effort':100}
            old_path=root/'config.json'
            old_path.write_text(json.dumps(old))
            with patch('config_and_hardware.user_data_dir', return_value=root), \
                 patch('config_and_hardware.os.cpu_count', return_value=2):
                cfg, configured = load_settings()
                self.assertTrue(configured)
                self.assertEqual(cfg['cpu_efforts'], [75, 100])
                save_settings(cfg)
                self.assertTrue((root/'config_v023.json').exists())
                self.assertFalse((root/'config_diag2.json').exists())
                self.assertTrue(cfg['diagnostics_enabled'])
                self.assertEqual(json.loads(old_path.read_text()),old)
                again, _ = load_settings()
                self.assertEqual(again['cpu_efforts'], [75, 100])

    def test_new_stratum_job_preserves_recent_hashrates(self):
        miner = MiningEngine()
        miner.rates[0]=(12345.0,time.monotonic())
        miner._broadcast({'job_id':'next'})
        self.assertEqual(miner.rates[0][0],12345.0)
        self.assertEqual(miner.jobs,1)
        self.assertEqual(miner.generation,1)


if __name__=='__main__':
    unittest.main()
