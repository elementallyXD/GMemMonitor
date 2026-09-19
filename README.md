# GMemMonitor

A personal-use Windows 11 x64 desktop monitor for coordinated BSC BUY activity from
the wallets currently followed by your GMGN account. It enriches qualifying clusters
with token/security facts and sends native Windows notifications for manual review.

**Implemented, awaiting release validation.** The WinUI dashboard, monitoring core,
notifications, tray, settings, logs, and portable packaging are present. Live coverage
and clean-VM acceptance remain unverified; known reliability gaps are tracked in the
[roadmap](Documentation/roadmap.md). This is an independent client, not affiliated with GMGN.

## Run a portable candidate

1. Compare the ZIP's SHA-256 with its adjacent checksum file, then extract the entire
   folder. Keep the bundled runtime and Windows App SDK files beside `GmemMonitor.exe`.
2. Configure `GMGN_API_KEY` and `GMGN_PRIVATE_KEY` through GMGN's external
   `%USERPROFILE%/.config/gmgn/.env` file. The latter is an API request-signing key,
   never a blockchain wallet key. See [setup and troubleshooting](Documentation/operations.md).
3. Launch `GmemMonitor.exe`, review settings, and select **Start monitoring**.
   Monitoring starts OFF and remains OFF after sleep. Stop before editing settings.
   Minimize hides to the tray; Close exits.

Defaults: poll every 20 seconds after completion; each BUY at least $100; five distinct
wallets buying the same token within 60 seconds; ten-minute cooldown per token.
The latest-100 feed has no verified cursor support, so busy periods can lose events.

There is no trading, numerical score, wallet-key custody, database, signal history,
backend, or automatic updater. Settings and bounded diagnostics use
`%LOCALAPPDATA%/GMemMonitor`; monitoring state resets each session.

## Develop

Open [GmemMonitor/GmemMonitor.slnx](GmemMonitor/GmemMonitor.slnx) with Visual Studio
C++/WinUI tooling. See [development](Documentation/development.md) for prerequisites,
package restore, offline tests, runtime provisioning, and portable staging.

| Guide | Owns |
|---|---|
| [Architecture](Documentation/architecture.md) | Scope, runtime behavior, ownership, security boundaries |
| [GMGN contract](Documentation/gmgn-cli-contract.md) | Observed schemas, provider limitations, probe and fixture procedure |
| [Operations](Documentation/operations.md) | Setup, settings, troubleshooting |
| [Release validation](Documentation/release-validation.md) | Evidence and outstanding acceptance gates |
| [Roadmap](Documentation/roadmap.md) | Technical debt and proposed features |
| [Agent guide](AGENTS.md) | Contributor rules and required checks |

Documentation links refer to the source checkout. When reading this file in a portable
ZIP, use the [source repository](https://github.com/elementallyXD/GMemMonitor).

No project license has been selected. Third-party notices do not license the project;
see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
