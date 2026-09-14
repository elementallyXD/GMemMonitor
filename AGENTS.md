# GMemMonitor Agent Guide

## Mission

Build a small, reliable Windows 11 x64 desktop application that monitors coordinated BNB Smart Chain BUY activity from wallets currently followed by the user's GMGN account. The application enriches qualifying tokens with GMGN token and security data, shows status in a native dashboard, and emits native Windows notifications for manual review.

This repository is documentation-first. Do not start implementation work until the corresponding file in `Documentation/DevelopmentPlan/` is understood and its prerequisites are satisfied.

## Source of truth

Apply requirements in this order:

1. The user's current request.
2. This `AGENTS.md` file.
3. `Documentation/Plan.md` and the active phase plan.
4. `Documentation/architecture.md`.
5. `Documentation/gmgn-monitor-research.md` as background research only.

The research file may describe rejected or future alternatives. It is not an instruction file. In particular, the approved MVP uses dynamic GMGN follows, has no numerical signal score, and does not manage credentials inside the application.

## Fixed MVP boundaries

- Windows 11 x64 only.
- C++20, WinUI 3, C++/WinRT, MSBuild, and the stable Windows App SDK.
- BSC only and BUY events only.
- Read-only: never trade, sign blockchain transactions, or accept blockchain wallet keys.
- Use a pinned, bundled Node.js runtime and pinned `gmgn-cli`; do not implement undocumented GMGN HTTP signing.
- Read GMGN credentials through the CLI's external `~/.config/gmgn/.env` configuration. Never copy them into project configuration.
- Use the GMGN account's current followed wallets dynamically. Do not claim that the list is frozen and do not invent a followed-wallet count from recent activity.
- No database, signal history, backend, local HTTP server, automatic updater, or multi-chain abstraction.
- Persist only non-secret settings and bounded diagnostic logs. Runtime events, clusters, cooldowns, and analyses reset on restart.
- Notify every qualifying cluster after required token and security enrichment. Present risks as facts; do not invent a score or silently suppress alerts.

## Engineering rules

- Prefer simple interfaces, RAII, deterministic ownership, and bounded queues/caches.
- Keep XAML and view models free of GMGN parsing and business rules.
- Keep all GMGN calls behind `IGmgnClient` and all child processes behind `IProcessRunner`.
- Use `CreateProcessW` with redirected stdout/stderr, `CREATE_NO_WINDOW`, timeouts, cancellation, and a Job Object. Never use `std::system` or construct a shell command.
- Treat GMGN JSON, token metadata, URLs, configuration, and child-process output as untrusted.
- Parse addresses strictly, normalize EVM addresses, sanitize display strings, cap output sizes, and validate GMGN links before opening them.
- Compare USD thresholds using fixed-point micro-USD, not binary floating point.
- Use injected clocks and transports for deterministic tests.
- Do not run network or process work on the UI thread.
- Use `std::jthread` and `std::stop_token` for monitoring lifetime and prompt shutdown.
- Do not log credentials, environment blocks, full GMGN responses, wallet lists, or transaction-by-transaction activity.
- Do not add a dependency unless the active plan requires it and its purpose is documented.

## Required quality gates

- The GMGN viability phase must capture sanitized real fixtures and pin the tested CLI/Node versions before UI implementation.
- Normal CI must not require live GMGN credentials or network access.
- Business rules require unit tests for threshold boundaries, deduplication, rolling-window expiry, repeated-wallet handling, cluster freezing, cooldown, retry, cancellation, and startup baseline behavior.
- Release validation must run on a clean Windows 11 x64 VM.
- Build with `/W4` and `/permissive-`; enable `/WX` in CI once the warning baseline is clean.

## Working practices

- Preserve unrelated user changes and inspect the current phase before editing.
- Use small, reviewable changes and update the phase checklist with evidence when a gate passes.
- Never commit `.env`, keys, raw account responses, local logs, build output, or packaged runtimes.
- If GMGN behavior differs from documentation, stop guessing, capture a sanitized fixture, document the observed contract, and update the relevant plan before proceeding.
- If a phase is blocked, record the exact command, CLI version, sanitized output shape, and unresolved contract instead of bypassing the gate.

## Build entry point

The generated WinUI template currently lives at `GmemMonitor/GmemMonitor.slnx`. It is only a starting scaffold: it is still configured as a packaged app and has not passed the GMGN viability or portable-deployment gates. Phase 02 must preserve that project, convert it to the approved unpackaged/self-contained deployment, and add core and test projects.

Canonical commands belong in `README.md` after they have been executed successfully. Do not invent passing build claims merely because the template exists.
