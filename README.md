# GMemMonitor

GMemMonitor is a Windows 11 native application in active development. It is intended to watch BUY activity from wallets currently followed by a user's GMGN account. When five distinct followed wallets each make a qualifying BNB Smart Chain purchase of the same token inside a rolling time window, GMemMonitor will retrieve GMGN token and security information and send a native Windows notification for manual investigation.

GMemMonitor is read-only. It does not trade, connect to a blockchain wallet, submit swaps, or provide investment advice. It is an independent client and is not affiliated with or endorsed by GMGN.

## Project status

The repository contains a WinUI 3 dashboard shell, core domain/aggregation/alert primitives, a bounded Windows child-process runner, and offline self-tests. The core GMGN client now classifies a CLI-reported HTTP 429 as a rate limit and suppresses further requests from that client during a conservative local cooldown. It does not and cannot change GMGN's server-side IP limit.

The dashboard is not yet wired to the monitoring controller: its monitoring button is
disabled and does not make a GMGN request. The live GMGN contract is therefore tested
with `GmgnContractProbe`, not by treating the current dashboard as a complete monitor.
The application has not passed the live-contract, full end-to-end, clean-VM, or
portable-release gates.

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

The follow-wallet response may contain a pagination token, while the currently documented CLI command may not expose a matching pagination option. Phase 01 must verify the live behavior. If pagination is unavailable, the MVP will process the latest 100 records per poll and explicitly avoid claiming lossless monitoring during unusually busy periods.

## Planned technology

| Area | Choice |
|---|---|
| Language | C++20 |
| UI | WinUI 3 with C++/WinRT |
| Build | Visual Studio/MSBuild `.sln` and `.vcxproj` |
| Windows platform | Existing template references Microsoft.WindowsAppSDK 2.4.0; verify and pin it in Phase 02 |
| Dependencies | NuGet for the Windows template; planned vcpkg manifest for application libraries |
| GMGN integration | Pinned `gmgn-cli --raw` through a bundled Node.js runtime |
| JSON | nlohmann/json |
| Logging | spdlog |
| Tests | GoogleTest |
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
3. vcpkg integration supplied by Visual Studio or a separately bootstrapped vcpkg checkout.
4. Node.js and npm for the viability and release-packaging phases. End users will receive a pinned Node runtime in the portable folder.
5. A GMGN API key and the request-signing credential required by the tested follow-wallet CLI contract.

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

The solution can be opened at `GmemMonitor/GmemMonitor.slnx`. The following commands were verified on this Windows 11 development machine after the template packages had already been restored:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  ".\GmemMonitor\GmemMonitor.slnx" /m /p:Configuration=Debug /p:Platform=x64

& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" --self-test
& ".\GmemMonitor\x64\Debug\GMemMonitor.MonitoringTests.exe"
```

If Visual Studio is installed in another location, open **Developer PowerShell for Visual Studio** and replace the first command with `MSBuild .\GmemMonitor\GmemMonitor.slnx /m /p:Configuration=Debug /p:Platform=x64`.

The `vcpkg.json` manifest declares the planned application dependencies. Its baseline and the GoogleTest project are held until the repository has a pinned vcpkg checkout; normal builds must remain offline after that bootstrap is complete.

## Documentation

- [Documentation home](Documentation/README.md)
- [Product brief](Documentation/Product/product-brief.md)
- [GMGN capability roadmap](Documentation/Product/gmgn-capability-roadmap.md)
- [GMGN access playbook](Documentation/Operations/gmgn-access-playbook.md)
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
