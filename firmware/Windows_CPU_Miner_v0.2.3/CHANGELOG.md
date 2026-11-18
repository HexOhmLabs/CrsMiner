# Changelog — CRSminer PC

## v0.2.3 — Automatic Stratum worker login (owner-tested, 18 November 2026)
- Automatically append `.worker` from the existing Worker name field when Wallet/account has no period.
- Preserve manually suffixed wallet/account strings exactly as entered.
- Use the same resulting username for Stratum authorization and share submission.
- Keep source mining logic, SHA engines, CPU controls, diagnostics and Fleet API unchanged from v0.2.2 except for the specified login behavior and version-specific configuration.
- Use `config_v023.json`, migrating earlier saved settings read-only on first launch.

## v0.2.2 — Optional diagnostics
- Toggle detailed diagnostic CSV logging in Settings.
- Add Open Diagnostics Folder and confirmation-protected Clear Diagnostic Files.

## v0.2.1-DIAG2 — CPU controls and performance diagnostics
- Individual effort 0–100% per logical CPU; Background/Dedicated modes; preserve at least one logical CPU at ≤75% effort.
- Per-worker CPU-time diagnostics and corrected hashrate reporting across new Stratum jobs.

## Source and validation
- This GitHub folder preserves the v0.2.3 application's original program files, native DLL and builder byte-for-byte. Documentation and GitHub packaging files were newly organized for publication.
- Windows has been tested by the project owner on AMD Ryzen 3 3200U and AMD E-350. The EXE is not supplied; users build on Windows x64.
