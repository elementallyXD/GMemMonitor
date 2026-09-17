# GMemMonitor Development Plan

This is the entry point for implementing the GMemMonitor MVP with Codex or another engineering agent. Complete phases in order. Do not begin product UI work before the GMGN viability contract is recorded with sanitized fixtures.

## Fixed product decision

GMemMonitor is a personal-use, read-only Windows 11 x64 application. It monitors BSC BUY activity from the GMGN account's current followed wallets, detects five-wallet convergence, enriches the token with GMGN token and security data, and sends a native notification. It does not score or trade tokens.

## Current repository state

The repository contains an unpackaged/self-contained WinUI 3 MVP at
`GmemMonitor/GmemMonitor.slnx`, the complete core session pipeline, sanitized successful
GMGN fixtures, native notifications, tray/lifecycle integration, and portable staging.
Offline gates pass locally. Live overlap/pagination observations and clean-VM Windows
acceptance remain external release-validation work; see
[implementation-audit.md](implementation-audit.md).

## Phase order

| Phase | Plan | Exit condition |
|---:|---|---|
| 01 | [GMGN viability](DevelopmentPlan/01-gmgn-viability.md) | A pinned CLI/Node pair and sanitized live contract fixtures exist. |
| 02 | [Foundation and transport](DevelopmentPlan/02-foundation-and-transport.md) | The existing solution builds in the target deployment model and the process boundary is fully testable offline. |
| 03 | [Monitoring and aggregation](DevelopmentPlan/03-monitoring-and-aggregation.md) | Deterministic polling, dedupe, and cluster tests pass. |
| 04 | [Enrichment and alerts](DevelopmentPlan/04-enrichment-and-alerts.md) | Qualifying clusters produce enriched, cooldown-controlled alert models. |
| 05 | [Windows application](DevelopmentPlan/05-windows-application.md) | Dashboard, tray, lifecycle, and notifications work on Windows 11. |
| 06 | [Testing and release](DevelopmentPlan/06-testing-and-release.md) | Clean-VM validation and portable x64 packaging pass. |

The [master implementation contract](DevelopmentPlan/00-master-plan.md) applies to every phase. The [architecture](architecture.md) defines component boundaries and data flow.

## Working with Codex

For each implementation session:

1. Ask Codex to read `AGENTS.md`, this file, the master plan, the active phase, and the architecture.
2. Give it only one phase or a clearly bounded checklist subset.
3. Require it to inspect the repository before changing files.
4. Require relevant tests and evidence before marking a checklist complete.
5. Do not provide GMGN credentials in the prompt. Live tests must load them from the external GMGN configuration and remain opt-in.
6. If an observed GMGN contract differs from research, update the contract documentation and affected plan before continuing.

## Global completion criteria

- All required business-rule and failure-path tests pass.
- No secret, raw account response, bundled runtime cache, generated package restore, or local log is tracked.
- The application remains responsive and shuts down without orphaned child processes.
- Settings cannot change during active monitoring.
- The app starts and resumes from sleep with monitoring OFF.
- A clean Windows 11 x64 machine can run the self-contained portable build.
- Documentation describes observed GMGN behavior accurately and does not claim lossless or real-time monitoring without evidence.
