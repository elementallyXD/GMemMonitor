# Release Validation Checklist

Use this checklist for the manual gates that cannot run safely in ordinary CI. Record the
date, Windows build, VM configuration, artifact SHA-256, and result. Never attach raw GMGN
responses, credentials, wallet lists, or transaction activity.

## Candidate preparation

```powershell
.\scripts\Provision-PinnedRuntime.ps1
.\scripts\Test-Workspace.ps1 -Configuration All
.\scripts\Stage-PortableRelease.ps1 -Version <major.minor.patch>
```

Confirm the ZIP checksum matches its `.sha256` file and inspect the ZIP: there must be no
`.env`, config, logs, dumps, symbols, AppX staging tree, source fixtures, or test binaries.

## Opt-in live contract

Only when no cooldown is active, run:

```powershell
.\scripts\Test-LiveGmgn.ps1 -EnableLiveGmgn -Token <public-bsc-contract>
```

For the remaining overlap/order observation, schedule two intentional feed probes at least
one normal polling interval apart and record only aggregate sanitized facts: record count,
ordering direction, stable-ID overlap count, and whether `next_page_token` was present.
Do not add an undocumented cursor option.

## Clean Windows 11 x64 VM

- Start from a VM with no global Node.js, gmgn-cli, or Windows App SDK installation.
- Extract the candidate ZIP and launch `GmemMonitor.exe`.
- Confirm first launch is OFF and missing external GMGN configuration gives actionable UX.
- Add valid user-owned GMGN configuration outside the app folder; verify Start and Stop.
- Trigger a controlled fake/live qualifying alert and verify the native notification.
- Activate the notification while running and after the app has exited; verify only the
  validated GMGN HTTPS URL opens in the default browser.
- Minimize and verify the window hides while the tray icon remains usable.
- Confirm tray Open, Start/Stop, and Exit mirror dashboard commands.
- Restart Explorer and verify exactly one tray icon is recreated.
- Suspend while polling/analysis is active; resume and verify monitoring remains OFF.
- Stop the app and replace its portable folder. Confirm `%LOCALAPPDATA%\GMemMonitor`
  settings and `%USERPROFILE%\.config\gmgn\.env` remain intact.
- Confirm exit leaves no `node.exe` owned by the candidate process tree.

## Reliability

- Run the accelerated deterministic fake-feed suite and retain the successful console log.
- Run a 24-hour idle/fake-feed session and sample private bytes, handles, log size, pending
  queue depth, and child-process count at start and end.
- Exercise repeated transient failures and verify bounded 1/2/4/8/16/30-second backoff,
  no notification storm, and bounded rotating logs.
- Exercise a hung child and verify Stop/Exit completes within the recorded timeout without
  an orphaned process.

Do not claim release acceptance until every row above has dated evidence.
