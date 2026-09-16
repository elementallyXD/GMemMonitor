# GMemMonitor

GMemMonitor is a Windows 11 native application in active development. It is intended to watch BUY activity from wallets currently followed by a user's GMGN account. When five distinct followed wallets each make a qualifying BNB Smart Chain purchase of the same token inside a rolling time window, GMemMonitor will retrieve GMGN token and security information and send a native Windows notification for manual investigation.

GMemMonitor is read-only. It does not trade, connect to a blockchain wallet, submit swaps, or provide investment advice. It is an independent client and is not affiliated with or endorsed by GMGN.

## Project status

The repository now contains the end-to-end MVP implementation: a WinUI 3 dashboard,
settings persistence, one cancellable polling session, bounded deduplication and rolling
aggregation, required token/security enrichment, native notifications, tray integration,
suspend/resume handling, bounded redacted logs, and verified portable-release staging.
All GMGN work remains behind the pinned CLI and off the UI thread.

Offline Debug and Release builds, parser/business-rule tests, process failure tests,
runtime-integrity checks, and portable staging run locally. Release readiness still
requires the explicitly manual gates: overlapping live-feed observations and the clean
Windows 11 VM matrix, including notification activation after process exit and Explorer
restart behavior. The app must not be represented as fully release-validated until those
checks are recorded.

See [Human testing](Documentation/human-testing.md) for the verified commands and safe rate-limit procedure.

## MVP behavior

- Windows 11 x64 native desktop dashboard with system-tray support.
- BSC BUY monitoring through the official `gmgn-cli` structured JSON interface.
- Dynamic use of the wallets currently followed by the authenticated GMGN account.
- Default 20-second polling interval.
- Minimum qualifying individual BUY of `$100.00`.
- Trigger at five distinct wallets buying the same token within 60 seconds.
- GMGN token-info and token-security enrichment before notification.
- Ten-minute notification cooldown per token.
- Native Windows notification with an action that opens GMGN in the default browser.
- Runtime-only event, cluster, analysis, and cooldown state.

The MVP does not calculate a 0–100 score. Every qualifying enriched cluster is notified, and GMGN security findings are displayed as factual warnings.

## Important GMGN limitations

GMGN documents a followed-wallet activity feed, but the reviewed public documentation does not expose an API that enumerates and freezes the complete followed-wallet list. GMemMonitor therefore uses the account's current follows dynamically and does not display an exact followed-wallet count.

The follow-wallet response contains a pagination token, but gmgn-cli 1.5.7 exposes no
verified supported cursor option. The MVP therefore processes the latest 100 records per
poll and explicitly does not claim lossless monitoring during unusually busy periods.

## Technology

| Area | Choice |
|---|---|
| Language | C++20 |
| UI | WinUI 3 with C++/WinRT |
| Build | Visual Studio/MSBuild `.sln` and `.vcxproj` |
| Windows platform | Pinned Microsoft.WindowsAppSDK 2.4.0 self-contained deployment |
| Dependencies | Pinned NuGet inputs for Windows components; vcpkg manifest/baseline recorded for approved native libraries |
| GMGN integration | Pinned `gmgn-cli --raw` through a bundled Node.js runtime |
| JSON | Strict bounded typed parser at the GMGN boundary |
| Logging | Bounded rotating UTF-8 file logger with centralized redaction |
| Tests | Offline C++ unit/contract/process test executable plus controlled fixtures |
| Concurrency | `std::jthread`, `std::stop_token`, and a small bounded executor |
| Distribution | Self-contained portable x64 folder/ZIP |

The Windows App SDK version must remain pinned and should be updated only through an explicit dependency change. The generated template currently records its Microsoft packages in `GmemMonitor/packages.config`; Phase 02 must verify that dependency set and remove no transitive package by guesswork. See Microsoft's [Windows App SDK downloads](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads) and [unpackaged WinUI deployment guide](https://learn.microsoft.com/windows/apps/package-and-deploy/unpackage-winui-app).

## Developer prerequisites

Install the following on Windows 11 x64:

1. Visual Studio 2026 with:
   - Desktop development with C++.
   - Windows application development/WinUI tooling.
   - MSVC C++ x64/x86 build tools.
   - Windows 11 SDK.
2. Git for Windows.
3. Node.js and npm only when provisioning the pinned portable runtime. End users receive
   that runtime in the portable folder.
4. A GMGN API key and the request-signing credential required by the tested follow-wallet CLI contract.

Do not install preview or experimental Windows App SDK releases for the MVP.

## GMGN account configuration

Follow GMGN's official [Agent API setup](https://docs.gmgn.ai/index/gmgn-agent-api) and [public-key generation guide](https://docs.gmgn.ai/index/generate-public-key):

1. Generate the GMGN authentication key pair locally.
2. Upload only the public key when creating the GMGN API key.
3. Create the GMGN CLI configuration file outside this repository at:

   ```text
   %USERPROFILE%\.config\gmgn\.env
   ```

4. Configure the exact variable names and values using the pinned CLI's official documentation. Do not copy an example credential file from this repository.

The authenticated CLI contract uses an API key and a GMGN request-signing key; the latter is not a blockchain trading-wallet key. Never paste either secret into source code, issue text, logs, screenshots, or an AI chat, and never place this `.env` file inside the repository.

GMGN's Agent API documentation states that only IPv4 requests are supported. The project does not change network routing or attempt to bypass GMGN controls.

## Build and offline test

The solution can be opened at `GmemMonitor/GmemMonitor.slnx`. From Developer PowerShell,
the verified local check builds x64 Debug and Release with warnings treated as errors,
runs all offline tests and the contract-probe self-test, scans tracked text for credential
material, verifies the pinned runtime tree, and performs a portable staging dry run:

```powershell
.\scripts\Provision-PinnedRuntime.ps1
.\scripts\Test-Workspace.ps1 -Configuration All
.\scripts\Test-AddressSanitizer.ps1
```

Runtime provisioning downloads the exact Node archive and npm packages recorded in the
manifests, then verifies their hashes. It is unnecessary when the matching ignored
`runtime` folder already exists.

## Portable package

After verification, create a versioned x64 ZIP and SHA-256 checksum:

```powershell
.\scripts\Stage-PortableRelease.ps1 -Version 0.1.0
```

The staging script copies only allowlisted self-contained application files and the exact
verified runtime tree described by `runtime-manifest.json`. It rejects symbols, AppX build
artifacts, test fixtures, logs, `.env` files, dumps, and credential-shaped content. Run
`GmemMonitor.exe` from the extracted folder. Because the personal-use MVP is unsigned,
Windows may show its normal reputation warning; inspect the SHA-256 checksum and
publisher/source before choosing to run it. Do not disable Windows security controls
globally.

Updates are manual: stop GMemMonitor and replace its portable folder. Settings under
`%LOCALAPPDATA%\GMemMonitor` and the user-owned `%USERPROFILE%\.config\gmgn\.env` remain
outside that folder and are preserved.

## Documentation

- [Documentation home](Documentation/README.md)
- [Product brief](Documentation/Product/product-brief.md)
- [GMGN capability roadmap](Documentation/Product/gmgn-capability-roadmap.md)
- [GMGN access playbook](Documentation/Operations/gmgn-access-playbook.md)
- [Release validation checklist](Documentation/Operations/release-validation.md)
- [Production-readiness review](Documentation/Reviews/production-readiness-review.md)
- [Architecture](Documentation/architecture.md)
- [Development plan](Documentation/Plan.md)
- [Implementation audit and next steps](Documentation/implementation-audit.md)
- [Human testing and GMGN setup](Documentation/human-testing.md)
- [GMGN CLI contract](Documentation/gmgn-cli-contract.md)
- [Research report](Documentation/gmgn-monitor-research.md)
- [Agent instructions](AGENTS.md)

## Security and privacy

- Treat all GMGN output and token metadata as untrusted.
- Never pass credentials on a command line.
- Allow browser navigation only to validated HTTPS GMGN URLs returned by GMGN.
- Keep logs bounded and redact secrets and account-sensitive data.
- Do not persist transaction feeds, wallet lists, signals, or token analyses.
- Never describe a GMGN risk field as a guarantee that a token is safe.

## License

No license has been selected. Until a license file is added, the source is not offered under an open-source license.
