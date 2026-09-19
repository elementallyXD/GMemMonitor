# Architecture

## Scope and decisions

The supported product is a personal-use Windows 11 x64, BSC BUY monitor. It uses the
GMGN account's **current server-side follows**, not a startup snapshot or a wallet list
in local settings. No exact followed-wallet count is available from the activity feed.

The application is read-only and has no scoring, transaction signing, blockchain wallet
keys, history database, backend, local HTTP server, telemetry service, or auto-updater.
All qualifying token types are eligible; wallet tags and token risk fields do not filter
the trigger. Trading, other chains, and credential custody require a separate product
decision. The discarded Base executor proposal belongs in a separate repository/process
with its own threat model and release gates.

Use the pinned CLI rather than undocumented HTTP signing. Unpackaged, self-contained
folder deployment keeps Node and Windows resources inspectable and replaceable together.
Exact dependency pins belong in the [build inputs](development.md#dependencies).

## Implemented ownership

| Location | Responsibility |
|---|---|
| [MainWindow](../GmemMonitor/MainWindow.xaml.cpp) | Settings controls, session composition, dispatcher callbacks; there is no separate MainViewModel |
| [MonitoringSession](../src/core/src/MonitoringSession.cpp) | Owns scheduler, controller, poller, analysis service/executor; coordinates cancellation |
| [Monitoring](../src/core/src/Monitoring.cpp) | State machine, baseline, deduplication, rolling clusters, poll worker |
| [Analysis](../src/core/src/Analysis.cpp) | Shared weighted admission, enrichment worker, retry queue, cooldown/delivery |
| [GmgnClient](../src/core/src/GmgnClient.cpp) / [parser](../src/core/src/GmgnJsonParser.cpp) | Fixed CLI operations, typed results/errors, untrusted JSON normalization |
| [ProcessRunner](../src/core/src/ProcessRunnerWin32.cpp) | No-shell child launch, restricted environment/handles, pipe capture, Job Object |
| [Alerts](../src/core/src/Alerts.cpp) / [WindowsNotificationService](../GmemMonitor/WindowsNotificationService.cpp) | Factual alert model, link validation, notification registration/delivery/activation |
| [TrayIconService](../GmemMonitor/TrayIconService.cpp) | Open/Start/Stop/Exit, minimize hiding, Explorer recreation, suspend/resume |
| [Settings](../src/core/src/Settings.cpp) / [Logging](../src/core/src/Logging.cpp) | Atomic non-secret config and rotating redacted diagnostics |

Data flow: poll → parse → baseline/dedupe → rolling cluster → frozen cluster →
token-info + token-security → cooldown check → Windows notification.

Public types live in [core headers](../src/core/include/gmemmonitor/core).
`IGmgnClient` returns typed result/error variants; `IProcessRunner` isolates process
execution. `IClock` supplies UTC for aggregation; `IAlertClock` supplies monotonic time
for cooldown. Scheduler/retry timers currently use the system steady clock directly.

## Monitoring and alert invariants

- Start snapshots validated settings and UTC start time. Older records seed dedupe but
  cannot trigger; records exactly at session start are eligible, including on the first page.
- USD is signed 64-bit micro-USD. Each individual BUY must meet the inclusive threshold;
  several smaller buys from one wallet are not summed.
- Dedupe uses a valid GMGN record ID, otherwise a SHA-256 key over canonical event fields.
  The LRU holds 1,000 keys; this is bounded duplicate protection, not permanent exactly-once delivery.
- Per token/wallet, retain qualifying events and select the largest unexpired BUY.
  Expiry is `timestamp < now - window`; equality stays eligible. Keeping later smaller
  BUYs matters when the previous maximum expires.
- At the distinct-wallet threshold, freeze representatives and erase the active token
  cluster. The triggering event is not reused.
- Require successful token-info and token-security responses with matching addresses.
  Optional risk facts can remain unavailable. There is no preliminary or scored alert.
- Cooldown is checked **after enrichment** and starts only when the notification service
  accepts delivery. Windows acceptance does not prove the user saw the notification.
  Invalid/missing GMGN links omit the button.
- Stop clears session state. Suspend requests cancellation promptly; resume completes
  cleanup and leaves monitoring OFF. Settings are disabled while a session is active.

## Threads and failure behavior

The UI owns controls and marshals worker updates through `DispatcherQueue`.
One poll `jthread` and one analysis `jthread` share a scheduler with one active CLI
request. Healthy polls wait the configured interval after completion/cluster submission.
A full analysis queue applies backpressure and can delay subsequent feed polls.

Feed authentication errors end polling; the dashboard tears down the session.
Enrichment authentication failure signals cancellation across the session. Other feed
failures enter `Retrying`, with 75–100% jitter over 1/2/4/8/16/30-second delays.
Enrichment retries rate limits, timeouts, and typed transient errors up to six retries;
terminal failures and rejected notifications are logged rather than guaranteed delivery.
The current CLI classification and rate-limit limitations are in the [contract](gmgn-cli-contract.md).

Normal Stop/Close joins workers synchronously. Prompt shutdown is an acceptance target,
not a proven guarantee for every child-process failure mode; see [technical debt](roadmap.md).

## Security and storage

The app resolves runtime files beside its executable and does not search PATH or use npx.
The process runner uses explicit quoting, NUL stdin, a handle allowlist, a minimal
Windows/user-location environment, and a kill-on-close Job Object. Parent-process GMGN
variables and Node injection variables are not inherited; the CLI loads its external file.

Default child limits are 30 seconds and 1 MiB per output stream. The JSON parser limits
depth to 64 and scalar length to 1,024 bytes. Malformed roots fail; invalid feed records
are counted and skipped. Display text is sanitized; optional fields do not imply safety.

Browser actions revalidate HTTPS and the exact `gmgn.ai` host, rejecting credentials
and explicit ports. Runtime hashes are verified by provisioning/staging scripts;
**the app currently checks file existence, not hashes, before launch**. Protect the
portable directory and external credentials from untrusted modification.

Only settings and logs are application-owned persistent state; their paths and recovery
procedure are in [operations](operations.md). Windows may retain notifications independently.
Memory bounds, pipe shutdown, and UI callback gaps are recorded in the roadmap.
