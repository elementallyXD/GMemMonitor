# Production-Readiness Review

## Verdict

The repository is a useful Phase 01/partial Phase 02 foundation, but it is **not
production-ready** and must not be represented as a functioning monitor. The WinUI 3
GUI shell is the right application type; it is intentionally disconnected from live
GMGN work until the contract fixture gate passes.

## What is in place

- Unpackaged/self-contained WinUI 3 C++/WinRT application shell.
- Core domain primitives, fixed-point USD parsing, rolling token/wallet aggregation,
  alert composition primitives, cooldown primitive, and offline smoke tests.
- Win32 child-process runner using `CreateProcessW`, output pipes, timeout,
  cancellation, and a Job Object.
- Credential-safe contract probe with bounded output and safe 429 classification.

## Blocking gaps

| Priority | Gap | Required resolution |
|---|---|---|
| P0 | Free-plan access blocks `follow-wallet`. | Obtain Plus access, then capture one sanitized success fixture. |
| P0 | No typed feed/token/security JSON parser or fixture-backed contract tests. | Implement only after the success fixture exists. |
| P0 | No monitoring controller, poll worker, shared scheduler, or live GUI binding. | Complete Phases 02–04 in order. |
| P0 | No Windows notifications, tray, lifecycle, persistence, or clean-VM test. | Implement Phase 05 after core services exist. |
| P1 | Display sanitization is byte-based and raw enrichment tax text can enter an alert. | Use Unicode-aware sanitization and sanitize every provider display field. |
| P1 | Rate cooldown is client-instance local; it is not a global scheduler. | Add one bounded, shared scheduler that honors provider reset times without shortening them. |
| P1 | Child process currently inherits interactive stdin and the complete environment. | Use NUL stdin, a handle allow-list, and a minimal documented environment. |
| P1 | Runtime manifest entry does not match the currently installed CLI layout. | Correct and validate the packaged runtime path before release staging. |
| P2 | Tests are smoke tests, not the planned GoogleTest/fixture/process suite. | Add deterministic unit, contract, process, and Windows integration coverage per plan. |

## Required engineering order

1. Resolve plan access and capture sanitized fixtures.
2. Finish parser, process-fixture, configuration, logging, and runtime-validation work.
3. Implement controller, baseline, deduplication key generation, non-overlapping polling,
   and global scheduling.
4. Implement required token/security enrichment and factual alert composition.
5. Connect the existing GUI, notifications, tray, configuration, and lifecycle behavior.
6. Complete security hardening, clean-VM validation, and portable staging.

## Guardrails

- Keep all live GMGN operations behind `IGmgnClient` and one scheduler.
- Keep all business and process work off the UI thread.
- Never log raw GMGN responses, credentials, wallet lists, or transaction activity.
- Preserve the read-only BSC MVP until a deliberate future-product decision changes it.
