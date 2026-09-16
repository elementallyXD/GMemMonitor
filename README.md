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

### Readiness verdict

The current build is ready for controlled human testing on the developer machine and on a
clean Windows 11 x64 VM. It is a release candidate, not yet a production-validated public
release. The implemented MVP workflow is:

1. Poll the authenticated GMGN account's current followed-wallet feed for BSC BUYs.
2. Accept only individual BUYs at or above the configured USD minimum.
3. Detect a configurable number of **distinct followed wallets** buying the same token
   inside the configured rolling window. Repeated BUYs from one wallet still count as one
   wallet toward the threshold.
4. Retrieve the required GMGN token and security facts.
5. Show a native Windows notification whose validated action opens the token on GMGN for
   a manual decision.

GMemMonitor never buys, sells, signs a blockchain transaction, or treats a GMGN risk field
as a guarantee. The remaining release evidence is the opt-in live overlap/pagination
observation, a 24-hour real-time soak, a successful clean-checkout CI run, and the complete
[clean-VM acceptance matrix](Documentation/Operations/release-validation.md).

Recent hardening verifies the downloaded Node archive before executing its bundled npm,
pins deterministic provisioning inputs, stages and re-verifies the exact runtime tree,
rejects duplicate monitoring starts before scheduler mutation, cancels queued enrichment
after authentication failure, and includes the Windows App SDK notification-registration
resource in the portable build.

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
3. NuGet CLI for command-line package restore, or use Visual Studio's NuGet restore.
4. Internet access while restoring packages and provisioning the pinned runtime. A global
   Node.js or npm installation is not required: the provisioning script downloads and
   verifies the pinned Node archive before using its bundled npm.
5. A GMGN API key and the request-signing credential required by the tested follow-wallet CLI contract.

Do not install preview or experimental Windows App SDK releases for the MVP.

## Install the portable application

The MVP is distributed as a portable ZIP, not an MSI/MSIX installer. End users do not need
Visual Studio, a global Node.js installation, gmgn-cli, or a separately installed Windows
App SDK.

1. Copy the versioned `GMemMonitor-<version>-win-x64.zip` and its adjacent `.sha256` file
   to the target Windows 11 x64 machine.
2. In PowerShell, verify that the actual hash matches the first value in the checksum file:

   ```powershell
   Get-FileHash .\GMemMonitor-<version>-win-x64.zip -Algorithm SHA256
   Get-Content .\GMemMonitor-<version>-win-x64.zip.sha256
   ```

3. Extract the **entire** ZIP to a user-writable permanent folder such as
   `C:\Apps\GMemMonitor`. Do not copy or run `GmemMonitor.exe` by itself.
4. Confirm the essential layout:

   ```text
   GmemMonitor.exe
   runtime\node.exe
   runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js
   runtime-manifest.json
   ```

5. Run `GmemMonitor.exe`. The unsigned personal-use build may produce a normal Windows
   reputation warning. Verify the checksum and source; do not disable Windows security
   controls globally.

The warning `Bundled GMGN runtime is missing` means the executable was moved out of the
portable folder or was launched directly from a Visual Studio output folder. Run the copy
inside the fully extracted portable folder.

## GMGN account configuration

Follow GMGN's official [Agent API setup](https://docs.gmgn.ai/index/gmgn-agent-api) and [public-key generation guide](https://docs.gmgn.ai/index/generate-public-key):

1. Generate the GMGN authentication key pair locally.
2. Upload only the public key when creating the GMGN API key.
3. Create the GMGN CLI configuration file outside this repository at:

   ```text
   %USERPROFILE%\.config\gmgn\.env
   ```

4. Configure the exact variable names and values using the pinned CLI's official documentation. Do not copy an example credential file from this repository.

   The file has this shape; replace the placeholders locally and never commit the result:

   ```dotenv
   GMGN_API_KEY=<key>
   GMGN_PRIVATE_KEY="<single-line PKCS#8 PEM with literal \n separators>"
   ```

5. Use the GMGN account associated with that API key to follow the wallets whose activity
   should be monitored. The application resolves the current followed-wallet set
   dynamically; no wallet list is configured in GMemMonitor.

The pinned `track follow-wallet` contract requires both values. `GMGN_PRIVATE_KEY` is an
OpenAPI request-signing key, not a blockchain wallet private key. The route may also require
a GMGN plan with enough request capacity; follow the current GMGN API/CLI documentation.

The authenticated CLI contract uses an API key and a GMGN request-signing key; the latter is not a blockchain trading-wallet key. Never paste either secret into source code, issue text, logs, screenshots, or an AI chat, and never place this `.env` file inside the repository.

GMGN's Agent API documentation states that only IPv4 requests are supported. The project does not change network routing or attempt to bypass GMGN controls.

## Start monitoring

1. Launch `GmemMonitor.exe`; monitoring always starts OFF.
2. Review the defaults: 20-second polling, `$100.00` minimum individual BUY, five distinct
   wallets, a 60-second aggregation window, and a ten-minute per-token notification
   cooldown.
3. Adjust the non-secret settings if needed and select **Save settings**. With the default
   threshold, at least five distinct followed wallets must each produce a qualifying BUY
   for the same BSC token inside the rolling window.
4. Select **Start monitoring**. A correct setup progresses from `Authenticating` to
   `Monitoring`.
5. When a cluster qualifies, wait for the required token/security enrichment. The native
   notification summarizes factual risk data; select **Open in GMGN** to inspect the token
   and decide manually whether to act.
6. Select **Stop monitoring** before changing settings, replacing the portable folder, or
   troubleshooting authentication/rate limits.

Only events at or after the current monitoring session starts are eligible. Runtime feed,
cluster, retry, analysis, and cooldown state resets when the application restarts. If GMGN
returns a rate limit, stop and wait for the reported reset time rather than repeatedly
starting the application.

For a VM, use an updated Windows 11 x64 guest with IPv4 internet access and Windows
notifications enabled. Configure `.env` under the VM user's `%USERPROFILE%`, extract the
complete ZIP inside the VM, and run the included executable. The full interactive checklist
is in [Release validation](Documentation/Operations/release-validation.md).

### Troubleshooting

- `Bundled GMGN runtime is missing`: use the executable inside the fully extracted portable
  folder and keep its `runtime` directory beside it.
- `Authentication required`: verify that `.env` belongs to the Windows user running the
  app, that the API key corresponds to the uploaded public key, and that the complete
  request-signing PEM is represented on one line with literal `\n` separators.
- `Rate limited`: stop all manual probes and application requests, then wait until the
  provider's reported reset time plus a safety margin. Do not loop retries.
- Monitoring is active but no notification appears: verify Windows notifications are
  enabled and remember that the threshold counts distinct followed wallets buying the
  same BSC token after the session started—not the number of transactions from one wallet.

Non-secret settings and bounded diagnostic logs live under `%LOCALAPPDATA%\GMemMonitor`.
Credentials stay under `%USERPROFILE%\.config\gmgn` and are never copied into the portable
folder.

## Build and offline test

The solution can be opened at `GmemMonitor/GmemMonitor.slnx`. From Developer PowerShell,
the verified local check builds x64 Debug and Release with warnings treated as errors,
runs all offline tests and the contract-probe self-test, scans tracked text for credential
material, verifies the pinned runtime tree, and performs a portable staging dry run:

```powershell
.\scripts\Provision-PinnedRuntime.ps1
nuget restore .\GmemMonitor\packages.config `
  -PackagesDirectory .\GmemMonitor\packages `
  -NonInteractive
.\scripts\Test-Workspace.ps1 -Configuration All
.\scripts\Test-AddressSanitizer.ps1
```

Runtime provisioning downloads the exact Node archive and npm packages recorded in the
manifests, then verifies their hashes. It is unnecessary when the matching ignored
`runtime` folder already exists.

## Create the portable ZIP yourself

From Developer PowerShell in the repository root, complete the restore, runtime provision,
and verification commands above. Do not package a build when any command fails. Then choose
an unused semantic version and run:

```powershell
.\scripts\Stage-PortableRelease.ps1 -Version 0.1.0
```

The version must use `major.minor.patch`. Staging intentionally refuses to overwrite an
existing folder, ZIP, or checksum; inspect old artifacts and choose a new version or remove
only the explicitly obsolete artifact yourself. Successful output is written to:

```text
artifacts\GMemMonitor-0.1.0-win-x64\
artifacts\GMemMonitor-0.1.0-win-x64.zip
artifacts\GMemMonitor-0.1.0-win-x64.zip.sha256
```

Verify the produced checksum before copying the ZIP to another machine:

```powershell
$zip = '.\artifacts\GMemMonitor-0.1.0-win-x64.zip'
(Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Get-Content "$zip.sha256"
```

The staging script copies only allowlisted self-contained application files and the exact
verified runtime tree described by `runtime-manifest.json`. It rejects symbols, AppX build
artifacts, test fixtures, logs, `.env` files, dumps, and credential-shaped content. Run
`GmemMonitor.exe` from the extracted folder. Because the personal-use MVP is unsigned,
Windows may show its normal reputation warning; inspect the SHA-256 checksum and
publisher/source before choosing to run it. Do not disable Windows security controls
globally.

Before treating a ZIP as a release rather than a local test candidate, execute and record
the clean Windows 11 VM, live-contract, and reliability gates in
[Release validation](Documentation/Operations/release-validation.md). The repository does
not currently produce an installer executable; MSI/MSIX, code signing, automatic updates,
and public distribution remain outside the MVP.

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
