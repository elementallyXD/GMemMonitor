# Roadmap and technical debt

The monitoring pipeline and Windows integration are implemented. Release acceptance is
still open in [release validation](release-validation.md). Items below are **planned**,
not capabilities or completed fixes. Resolve reliability gaps before adding features.

## Before release

| Priority | Source-backed gap | Completion criterion |
|---|---|---|
| P0 | [GMGN scheduler](../src/core/include/gmemmonitor/core/Analysis.h) uses feed weight 3 and fixed 20/20; current provider docs list weight 10 and plan-specific limits | Revalidate the pinned CLI/account contract, update admission policy and deterministic tests; preserve the observed mismatch in the contract |
| P0 | [Rate-limit parser](../src/core/src/GmgnClient.cpp) recognizes limited text, caps waits at 300 seconds, and does not parse `reset_at`; generic nonzero network/5xx exits are not classified transient for enrichment | Handle documented limit/error shapes and full deadlines, including long bans, without extra requests; test missing-config/auth and retry classification |
| P0 | [ProcessRunner](../src/core/src/ProcessRunnerWin32.cpp) joins pipe readers after parent exit before closing its Job Object | Add a child that exits while a descendant holds inherited pipes; ensure Stop/timeout bounds the drain and cleans up the tree |
| P1 | [Analysis retry queue](../src/core/src/Analysis.cpp) can requeue after a producer refills the freed slot; `TrySubmit` checks equality with capacity | Enforce capacity across admission and retries under saturation/cancellation without silently losing qualifying clusters |
| P1 | [Aggregator](../src/core/src/Monitoring.cpp) lacks a hard event cap/future-time rejection; [cooldown map](../src/core/src/Alerts.cpp) never prunes within a session | Define bounded retention and clock-skew handling; test adversarial timestamps, sustained unique tokens, and dedupe eviction/replay |
| P1 | [Dashboard callbacks](../GmemMonitor/MainWindow.xaml.cpp) have no session-generation check; Stop joins on the UI thread; analysis failures only reach logs | Reject stale updates after Stop/restart and test rapid lifecycle changes; show actionable failure/freshness state without blocking UI |
| P1 | [Startup](../GmemMonitor/MainWindow.xaml.cpp) checks runtime existence, whereas hashes are checked only by build scripts | Decide and test runtime integrity verification before child execution; document the installed-folder trust boundary |
| P1 | Live ordering/ID overlap and latest-100 coverage are unresolved; probe exposes only syntax/byte summaries | Add an opt-in in-memory comparison that reports aggregate counts only; never save raw records or invent a cursor flag |

## Maintenance

- Reconcile the unused [vcpkg manifest](../vcpkg.json) with the custom parser/logger/tests;
  remove unused dependency declarations after verifying builds. Do not add libraries
  merely to match an obsolete design.
- Expand malformed-input coverage for custom parsers and URL handling; inject worker
  timing where practical so retry and saturation tests avoid wall-clock races.
- Audit duplicated probe/process/parser logic when changing transport behavior; the probe
  is a separate implementation, not evidence that every application path behaves identically.
- Record reproducible CI/toolchain and Release test execution. The workflow currently runs
  Debug tests and uses a moving hosted runner.
- Complete third-party license/copyright inventory and choose a project license before
  public distribution. Review signing and executable version stamping separately from ZIP naming.

## Useful features after acceptance

| Order | Proposed feature | Scope/acceptance |
|---|---|---|
| 1 | Feed-health display | Actual last-success age, rejected-record count, queue delay, and possible truncation warning; no invented total-wallet count |
| 2 | Offline notification test | Clearly labelled synthetic alert with no GMGN call; supports delivery and post-exit activation tests on a clean VM |
| 3 | Local diagnostic summary | Versions, runtime integrity, safe error categories and counters; preview/redact before user-controlled export |
| 4 | Richer factual token context | Liquidity/holder concentration from a verified pinned contract, with units, source time, and explicit unavailable values |
| 5 | Coverage improvements | Supported cursor or inclusive server-side USD filtering only after contract fixtures and boundary/overlap tests demonstrate correctness |

Keep these changes Windows 11 x64, BSC-only, and read-only. No numerical signal score,
trading executor, credential manager, persistent signal history, or multi-chain framework
is implied by this roadmap.
