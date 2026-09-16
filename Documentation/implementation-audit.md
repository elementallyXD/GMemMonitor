# Implementation Audit — 2026-09-16

## Outcome

The planned MVP is implemented end to end and its offline gates pass on the development
machine. The application starts OFF, loads only non-secret settings, invokes the pinned
bundled Node/GMGN CLI without a shell, monitors dynamic BSC BUY activity on one worker,
freezes exact rolling clusters, performs required enrichment through the shared scheduler,
and emits factual native notifications. Tray, close, suspend/resume, logging, and portable
staging paths are present.

This is not yet a release-acceptance claim. Two evidence classes require external/manual
execution and remain open:

- repeated opt-in live feed observations for ordering, ID overlap, and the unresolved
  unsupported pagination token;
- the clean Windows 11 x64 VM matrix for notification activation, Explorer restart,
  sleep/resume, browser launch, and portable folder replacement.

## Phase review

| Phase | Implemented evidence | Remaining external evidence |
|---:|---|---|
| 01 | Pinned Node 24.12.0 and gmgn-cli 1.5.7; successful sanitized feed, token-info, and token-security fixtures; credential-safe probe | Repeated poll ordering/overlap observations; no supported CLI pagination option is known |
| 02 | Typed parsers/errors, schema-versioned atomic settings, bounded redacted logger, hardened CreateProcessW boundary, process fixtures | None for the offline code gate |
| 03 | Controller, startup baseline, SHA-256 fallback dedupe, exact rolling clusters, completion-relative non-overlapping polling, cancellation/backoff | Live feed cadence/overlap observation |
| 04 | Shared weighted 20/20 scheduler (feed weight 3, enrichment weight 1), bounded analysis queue, token/security enrichment, factual alerts, validated GMGN URLs, delivery-based cooldown | Live enrichment revalidation when intentionally scheduled |
| 05 | Dashboard dispatch, persistence, native notifications, browser activation, stable-GUID tray, Explorer recreation, minimize/close and power handling; local staged launch, notification registration, responsive window, and clean exit smoke passed | Interactive clean-VM acceptance and notification activation after app exit |
| 06 | Warning-clean Debug/Release build, Visual Studio static analysis, AddressSanitizer, expanded unit/contract/process tests, accelerated deterministic soak, CI workflow, secret scan, pinned runtime tree verification, allowlisted ZIP/checksum staging | Clean-checkout CI execution, 24-hour real-time soak, clean-VM matrix |

## Verified local commands

```powershell
.\scripts\Provision-PinnedRuntime.ps1
.\scripts\Test-Workspace.ps1 -Configuration All
.\scripts\Test-AddressSanitizer.ps1
.\scripts\Stage-PortableRelease.ps1 -Version 0.1.0
```

The live probe remains opt-in and must follow the one-request-and-stop procedure in
[human-testing.md](human-testing.md). No live request is part of normal CI.

## Known upstream limitation

The feed response exposes `next_page_token`, but gmgn-cli 1.5.7 has no verified public
cursor argument for `track follow-wallet`. The MVP therefore requests the latest 100
records and does not claim lossless monitoring during unusually busy periods.
