"""Local settings and lightweight Windows hardware inventory."""
import json
import os
import platform
import re
import socket
import subprocess
from pathlib import Path


def user_data_dir() -> Path:
    if os.name == 'nt':
        root = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData' / 'Local')
        return root / 'HexOhm' / 'CRSminerPC'
    return Path.home() / '.config' / 'HexOhm' / 'CRSminerPC'


def normalize_cpu_efforts(values, cpu_count=None):
    """Keep one saved effort per logical CPU, padding new CPUs as disabled."""
    count = cpu_count if cpu_count is not None else (os.cpu_count() or 1)
    if not isinstance(values, list):
        raise ValueError('CPU efforts must be a list of integer percentages.')
    result = []
    for value in values[:count]:
        if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 100:
            raise ValueError('Each CPU effort must be a whole number from 0 to 100%.')
        result.append(value)
    return result + [0] * (count - len(result))


def validate_cpu_allocation(cfg, cpu_count=None):
    count = cpu_count if cpu_count is not None else (os.cpu_count() or 1)
    mode = cfg.get('mining_mode', 'background')
    if mode not in ('background', 'dedicated'):
        raise ValueError('Mining mode must be Background or Dedicated.')
    efforts = cfg.get('cpu_efforts')
    if not isinstance(efforts, list) or len(efforts) != count:
        raise ValueError('CPU settings must include exactly one entry for each detected logical CPU.')
    normalized = normalize_cpu_efforts(efforts, count)
    if not any(normalized):
        raise ValueError('Enable at least one logical CPU above 0% to start mining.')
    if not any(value <= 75 for value in normalized):
        raise ValueError('At least one logical CPU must be limited to 75% effort or less.')
    return normalized


def default_settings() -> dict:
    cores = os.cpu_count() or 1
    selected = min(2, max(1, (cores + 1) // 2))
    return dict(name='pc01', pool='', wallet='', worker='pc',
                password='', suggested_difficulty='', threads=selected,
                effort=50, cpu_efforts=[50 if i < selected else 0 for i in range(cores)],
                mining_mode='background', theme='dark', fleet=True, diagnostics_enabled=True)


def load_settings() -> tuple[dict, bool]:
    cfg = default_settings()
    # v0.2.3 stores settings separately; first launch imports v0.2.2 or DIAG2 read-only.
    path = user_data_dir() / 'config_v023.json'
    if not path.exists():
        path = user_data_dir() / 'config_v022.json'
    if not path.exists():
        path = user_data_dir() / 'config_diag2.json'
    if not path.exists():
        path = user_data_dir() / 'config.json'  # legacy settings read-only
    if not path.exists():
        return cfg, False
    try:
        saved = json.loads(path.read_text(encoding='utf-8'))
        if isinstance(saved, dict):
            for key in cfg:
                if key in saved:
                    cfg[key] = saved[key]
            # Upgrade v0.2.1 global CPU settings to an individual logical-CPU map.
            # The protected-CPU rule also applies to old configurations at 100%.
            if 'cpu_efforts' not in saved:
                n = min(os.cpu_count() or 1, max(1, int(cfg.get('threads', 1))))
                effort = max(0, min(100, int(cfg.get('effort', 50))))
                cfg['cpu_efforts'] = [effort if i < n else 0 for i in range(os.cpu_count() or 1)]
                if all(v > 75 for v in cfg['cpu_efforts']):
                    cfg['cpu_efforts'][0] = 75
            else:
                # On different hardware, never automatically activate new CPUs.
                try:
                    cfg['cpu_efforts'] = normalize_cpu_efforts(cfg['cpu_efforts'])
                except ValueError:
                    cfg['cpu_efforts'] = default_settings()['cpu_efforts']
            # Upgrade old configuration: preserve any explicitly configured login.
            # The old pool_user field is removed from the new UI and file.
            if str(saved.get('pool_user', '')) and not str(cfg.get('wallet', '')).strip():
                cfg['wallet'] = str(saved['pool_user'])
            return cfg, bool(cfg.get('pool') and str(cfg.get('wallet', '')).strip())
    except (OSError, ValueError):
        pass
    return cfg, False


def save_settings(cfg: dict):
    path = user_data_dir() / 'config_v023.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name('config_v023.json.tmp')
    try:
        with temp.open('w', encoding='utf-8') as f:
            json.dump({key: cfg[key] for key in default_settings()}, f, ensure_ascii=False, indent=2)
        os.replace(temp, path)
    finally:
        if temp.exists():
            temp.unlink()


def validate_settings(cfg: dict):
    from miner_core import parse_pool_url, target_from_difficulty
    if not re.fullmatch(r'[a-zA-Z0-9]{4}', str(cfg.get('name', ''))):
        raise ValueError('Miner ID must be exactly 4 letters or digits.')
    parse_pool_url(str(cfg.get('pool', '')))
    if not str(cfg.get('wallet', '')).strip():
        raise ValueError('Enter the pool account / wallet used for mining.authorize.')
    if any(ch in str(cfg.get('wallet', '')) for ch in '\r\n'):
        raise ValueError('Wallet / pool account must not contain newline characters.')
    if not re.fullmatch(r'[a-zA-Z0-9_-]{1,30}', str(cfg.get('worker', ''))):
        raise ValueError('Worker must contain 1–30 letters, digits, _ or -.')
    validate_cpu_allocation(cfg)
    diff = str(cfg.get('suggested_difficulty', '')).strip()
    if diff:
        target_from_difficulty(diff)
    if '\n' in str(cfg.get('password')) or '\r' in str(cfg.get('password')):
        raise ValueError('Pool password must not contain newline characters.')


def hardware_info() -> dict:
    cores = os.cpu_count() or 1
    name = platform.processor() or platform.machine() or 'Unknown processor'
    mhz = ''
    if os.name == 'nt':
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as k:
                name = winreg.QueryValueEx(k, 'ProcessorNameString')[0].strip() or name
                try:
                    mhz = str(winreg.QueryValueEx(k, '~MHz')[0]) + ' MHz nominal'
                except OSError:
                    pass
        except OSError:
            pass
    ram = 'Not detected'
    if os.name == 'nt':
        try:
            import ctypes
            class MemoryStatus(ctypes.Structure):
                _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong),
                            ('ullTotalPhys', ctypes.c_ulonglong), ('ullAvailPhys', ctypes.c_ulonglong),
                            ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                            ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong),
                            ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
            memory = MemoryStatus()
            memory.dwLength = ctypes.sizeof(memory)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
                ram = f'{memory.ullTotalPhys / (1024 ** 3):.1f} GiB'
        except Exception:
            pass
    else:
        try:
            ram = f'{(os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")) / (1024 ** 3):.1f} GiB'
        except (ValueError, OSError, AttributeError):
            pass
    return dict(cpu=name, cpus=cores, ram=ram, clock=mhz or 'N/A',
                os=platform.platform(), architecture=platform.machine(),
                hostname=socket.gethostname(), gpu='Detecting…' if os.name == 'nt' else 'Windows detection only')


def detect_windows_gpu() -> str:
    if os.name != 'nt':
        return 'Windows GPU inventory only'
    try:
        flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
        cp = subprocess.run(
            ['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
             'Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name'],
            capture_output=True, text=True, timeout=8, creationflags=flags)
        found = list(dict.fromkeys(line.strip() for line in cp.stdout.splitlines() if line.strip()))
        return ' / '.join(found[:4]) if found else 'No GPU name returned'
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return 'Unavailable (GPU mining is not used)'
