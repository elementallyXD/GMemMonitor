# Production-Readiness Review

## Verdict

The MVP implementation is feature-complete for its approved read-only scope and passes
the local offline verification matrix. It remains a **release candidate**, not a fully
validated release, until the opt-in live observations, soak tests, and clean Windows 11
x64 VM checklist are recorded.

## Implemented controls

- Unpackaged, self-contained WinUI 3 application that always starts OFF.
- Dynamic current-follow BSC BUY monitoring through pinned gmgn-cli; no scoring/trading.
- Strict typed parsing, fixed-point micro-USD, bounded dedupe/analysis/output/log state.
- CreateProcessW with explicit arguments, NUL stdin, handle allowlist, sanitized child
  environment, timeouts/cancellation, and kill-on-close Job Object containment.
- Startup baseline, completion-relative non-overlapping polls, reset-aware rate-limit
  suppression, bounded retries, and authentication stop behavior.
- Required token-info/security enrichment and factual alerts only.
- Unicode/control sanitization and exact-host HTTPS GMGN URL validation at both model and
  browser boundaries.
- Atomic non-secret settings, rotating redacted diagnostics, notifications, stable-GUID
  tray icon, Explorer recreation, close/minimize behavior, and OFF-after-resume behavior.
- Runtime version/hash/tree verification, release extension allowlist, prohibited-content
  and secret scans, versioned ZIP, and SHA-256 checksum.

## Remaining release gates

| Gate | Required evidence |
|---|---|
| Live contract | Intentional repeated polls record sanitized ordering/ID overlap and confirm the pagination limitation is unchanged |
| Clean VM | First launch without global Node/App SDK, missing-config UX, Start/Stop, notification delivery/activation, browser action, tray, Explorer restart, suspend/resume, replacement update |
| Reliability | Accelerated fake-feed soak plus 24-hour idle/fake-feed handle, memory, log, queue, and child-process observations |
| CI | Successful run of the checked-in Windows workflow from a clean checkout |

## Release guardrails

- Never package or log `.env`, credentials, raw account responses, wallet lists, or
  transaction-by-transaction activity.
- Never claim lossless monitoring while the latest-100/no-cursor limitation remains.
- Never label a token safe or attach a numerical score.
- Do not publish the candidate until the remaining evidence is recorded.
