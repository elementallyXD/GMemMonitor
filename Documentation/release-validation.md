# Release validation

**Release acceptance is open.** Code presence, a passing build, and historical local
success reports do not establish live correctness or clean-Windows portability.
Complete the [roadmap's pre-release fixes](roadmap.md#before-release) and the checks below
before representing a candidate as release-validated.

## Evidence record

Historical project notes dated 2026-09-16 reported Debug/Release builds, static analysis,
ASan, offline unit/contract/process tests, accelerated synthetic soak, runtime verification,
portable staging, and a local app launch/registration/clean-exit smoke. Those reports
contained no clean-VM acceptance record or CI run link.

| Date | Revision/artifact | Check | Result |
|---|---|---|---|
| 2026-09-14 | Node 24.12.0 / CLI 1.5.7 | Prior live shape captures | Three sanitized fixtures checked in; see [contract](gmgn-cli-contract.md) |
| 2026-09-19 | 5394417 + documentation working tree | Source/docs review | Confirmed implementation; 75 local links/anchors pass; contract drift and reliability gaps recorded; no live requests |
| 2026-09-19 | Same working tree; Windows 10.0.26200, MSBuild 18.10.1 | Test-Workspace.ps1 -Configuration All | Pass: Debug/Release /WX builds, Release static analysis, Debug tests/probe self-test, credential scan; dry-run validated 298 app files and runtime tree |
| 2026-09-19 | Same machine and working tree | Test-AddressSanitizer.ps1 | Pass: instrumented offline tests |

Add new results here with source revision, command, date, Windows build/toolchain and
artifact SHA-256 where applicable. Record failures and skipped checks explicitly.
Retain only sanitized evidence; never commit raw account responses, credentials,
wallet lists, transaction activity, or local diagnostic logs.

## Candidate gates

- [x] Full [development verification](development.md#build-and-test), including ASan.
- [ ] Successful [Windows CI](../.github/workflows/ci.yml) run from a clean checkout,
  with run link and exact revision.
- [ ] Actual versioned ZIP staging, not only `-DryRun`; verify the adjacent checksum,
  runtime tree, notification resource DLL, and notices.
- [ ] Inspect archive for credentials, user settings, logs, dumps, PDBs, AppX staging
  trees, source fixtures, and test binaries; none may ship.
- [ ] Complete licensing/notices and version identity review before public distribution.

## Opt-in live contract

Use the [probe procedure](gmgn-cli-contract.md#probe-and-fixtures), respecting full
provider cooldowns. Do not induce live failures or guess flags.

- [ ] Revalidate endpoint entitlement and server admission weights for the pinned CLI/account.
- [ ] Record feed/info/security success with sanitized shapes if contracts change.
- [ ] Observe intentional overlapping polls at least one normal poll interval apart:
  ordering direction, record count, stable-ID overlap count, and page-token presence.
  No raw capture. The current probe cannot report these comparisons; implement the
  privacy-preserving observer first.
- [ ] Record remaining latest-100 coverage limitations; do not call the feed lossless.

## Clean Windows 11 x64 VM

Use a VM with no global Node, gmgn-cli, or Windows App SDK. Record Windows build,
VM details, exact candidate hash, date, and result for each case.

- [ ] Extract the complete ZIP; launch without installing those runtimes globally.
- [ ] First launch OFF; missing external configuration gives actionable feedback.
- [ ] Configure the VM user's external GMGN file; verify explicit Start/Stop.
- [ ] Deliver an enriched notification; verify Windows settings and factual risk display.
- [ ] Activate its GMGN action while running and after process exit; only validated HTTPS opens.
- [ ] Minimize hides to tray; tray Open/Start/Stop/Exit and Close behave correctly.
- [ ] Restart Explorer; exactly one tray icon returns.
- [ ] Suspend during polling/enrichment; resume OFF; no stale notification or state update.
- [ ] Exit leaves no child process. Replace the portable folder; user settings and external
  credentials remain intact.

There is currently no synthetic notification UI or fake-feed dashboard mode. Core fake
clients do not validate Windows delivery. Use an explicitly scheduled live scenario or
implement the offline test facility before marking the notification rows passed.

## Reliability

- [ ] Record a 24-hour idle/controlled-feed session with start/end private bytes, handle
  count, log size, child-process count, and queue measurements where instrumentation exists.
- [ ] Exercise transient failures, saturation, retries, repeated Start/Stop, and system-clock
  changes with controlled transports; verify bounded resources and no notification storm.
- [ ] Exercise timeout/cancellation and parent-exits-first/pipe-holding descendants; record
  Stop/Exit duration and confirm process-tree cleanup.

The accelerated core soak covers synthetic event progression only. A GUI fake-feed soak
and queue instrumentation need a harness; do not mark them complete from core tests.
