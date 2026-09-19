# Development

## Prerequisites

Use Windows 11 x64 with Visual Studio C++ desktop and WinUI tooling, a Windows SDK,
and the C++ AddressSanitizer component. The development installation uses Visual Studio
2026; projects select v145 on VS 18+ and v143 otherwise. Older toolchains and non-x64
solution configurations are not release-validated.

Run PowerShell from the repository root. Git and NuGet CLI must be available for the
commands below; Visual Studio NuGet restore is an alternative when `nuget` is absent.
No global Node/npm, vcpkg installation, or GMGN credentials are needed for offline tests.

## Build and test

Restore the pinned NuGet packages once (network required unless cached):

~~~powershell
nuget restore ./GmemMonitor/packages.config -PackagesDirectory ./GmemMonitor/packages -NonInteractive
~~~

Provision the pinned runtime for packaging, then run the complete local gate:

~~~powershell
./scripts/Provision-PinnedRuntime.ps1
./scripts/Test-Workspace.ps1 -Configuration All
./scripts/Test-AddressSanitizer.ps1
~~~

Provisioning downloads a hash-verified Node archive and runs its bundled npm against the
committed lockfile with lifecycle scripts disabled. An already matching runtime is
verified and reused. A mismatched runtime requires inspection before using `-Force`.

`Test-Workspace` builds Debug/Release x64 with warnings as errors, runs Release static
analysis on core/tests, executes **Debug** tests and the probe self-test, scans working-tree
text for credential patterns, and checks portable staging inputs. It does not execute the
Release test binary. Release-only runs omit executable tests; do not use their final
summary as evidence that tests ran.

For a quick offline edit/test cycle after NuGet restore:

~~~powershell
./scripts/Test-Workspace.ps1 -Configuration Debug -SkipPortableDryRun
~~~

`-SkipCodeAnalysis` and `-SkipPortableDryRun` are development shortcuts, not release gates.
The ASan script rebuilds core/tests separately and runs the suite with the MSVC sanitizer.
For verification evidence and its limits, see [release validation](release-validation.md).

## Test map

| Source | Coverage |
|---|---|
| [MonitoringSelfTests.cpp](../tests/unit/MonitoringSelfTests.cpp) | Thresholds, startup baseline, repeated wallets, expiry/freezing, dedupe, parsing/fixtures, settings, logs, scheduler, cooldown, retries, cancellation, session integration, accelerated synthetic soak |
| [ProcessFixture.cpp](../tests/process-fixtures/ProcessFixture.cpp) | Controlled child success/failure, output limits, timeout/cancellation, process-tree cleanup |
| [GmgnContractProbe](../tools/GmgnContractProbe/main.cpp) | Offline syntax/argument self-test and separately opt-in live probes |

Tests use custom assertions, fake clients, and injected business-rule clocks, not GoogleTest.
Clock injection is incomplete for worker timers. The accelerated soak is not a 24-hour
wall-clock observation. UI/Windows acceptance is manual; no dashboard fake-feed mode exists.

Solution outputs differ: tests are under `GmemMonitor/x64/Debug`, the probe under
`GmemMonitor/bin/x64/Debug`, and the app under
`GmemMonitor/x64/<Configuration>/GmemMonitor`. Standalone projects use their own output
directories. The scripts select the appropriate executables.

## Dependencies

| Authority | Purpose |
|---|---|
| [packages.config](../GmemMonitor/packages.config) and [app project](../GmemMonitor/GmemMonitor.vcxproj) | Exact NuGet pins/imports, including Windows App SDK 2.4.0; do not equate the meta-package version with every component version |
| [runtime-manifest.json](../packaging/runtime-manifest.json) | Node 24.12.0, gmgn-cli 1.5.7, paths, archive/file/tree integrity |
| [package.json](../packaging/gmgn-cli/package.json) / [lockfile](../packaging/gmgn-cli/package-lock.json) | Exact npm dependency graph for provisioning |
| [vcpkg.json](../vcpkg.json) | Unused legacy manifest for nlohmann-json, spdlog, and gtest; current projects use custom implementations |
| [THIRD_PARTY_NOTICES.txt](../THIRD_PARTY_NOTICES.txt) | Redistribution notices; completeness still requires release review |

Do not update dependency versions as part of routine restore. A runtime upgrade must
update pins/lockfile/hashes together, revalidate the CLI contract with sanitized fixtures,
and repeat offline and clean-VM gates. Avoid guessing which Windows SDK transitive packages
can be removed.

## Portable staging

After the full gate passes, choose an unused `major.minor.patch` artifact version:

~~~powershell
./scripts/Stage-PortableRelease.ps1 -Version 0.1.0
~~~

The script creates a folder, ZIP, and adjacent SHA-256 under `artifacts`, refusing to
overwrite existing outputs. The version names artifacts; it does not stamp the executable.
It verifies runtime versions/hashes/tree, allowlists app extensions, copies the full verified
runtime, and scans staged content. `-DryRun` checks inputs but does not create/scan a complete
ZIP; actual staging is a separate release gate.

Run the staged executable to monitor: raw Visual Studio app output lacks its sibling runtime.
The ZIP includes README/notices, not the source documentation; README links back to the
source repository.

[Windows CI](../.github/workflows/ci.yml) provisions dependencies, restores NuGet, and runs
the full workspace and ASan commands. Its GMGN tests are offline, but setup uses network
access. The moving `windows-latest` runner/toolchain still needs a recorded clean-checkout run.
