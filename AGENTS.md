# GMemMonitor agent guide

## Before changing code

Read [README](README.md), [architecture](Documentation/architecture.md), and the relevant
[roadmap](Documentation/roadmap.md) item. Implementation exists; the former phase plans
are obsolete. Check prerequisites and release gaps before extending it.

Authority: current user request, this guide, architecture/product constraints, then
the relevant topic guide. Source code establishes what is implemented; documentation
must distinguish intended behavior from defects and unverified claims.

## Product constraints

- Windows 11 x64; C++20, WinUI 3, C++/WinRT, MSBuild, stable pinned Windows App SDK.
- BSC BUYs from the account's dynamic GMGN follows. No frozen list or inferred total count.
- Read-only: no trading, transaction signing, blockchain wallet keys, or numerical score.
- Use bundled pinned Node/gmgn-cli. Never reverse-engineer HTTP signing or bypass access limits.
- Credentials remain in the CLI's external user configuration; the app must not manage them.
- Persist only non-secret settings and bounded diagnostics. No history database, backend,
  local HTTP server, automatic updater, or multi-chain abstraction.
- Enrich qualifying clusters before notification. Show risks as facts, never safety
  guarantees; do not add undisclosed filtering. Cooldown and failures must be explicit.

## Engineering rules

- Preserve unrelated changes; prefer small changes, RAII, deterministic ownership, and
  bounded resources. Document and justify any new dependency.
- Keep GMGN parsing/business rules outside XAML. All GMGN calls go through `IGmgnClient`;
  all child processes go through `IProcessRunner`.
- Use `CreateProcessW`, redirected bounded output, `CREATE_NO_WINDOW`, timeout,
  cancellation, and a Job Object. Never use a shell or `std::system`.
- Treat JSON, settings, metadata, paths, URLs, and process output as untrusted.
  Normalize strict EVM addresses, sanitize display text, validate GMGN links at opening,
  and compare USD thresholds using integer micro-USD.
- Keep network/process work off the UI thread. Use `std::jthread`/`std::stop_token`
  for session lifetime and injected clocks/transports for business-rule tests.
- Never log or commit credentials, environment blocks, raw GMGN responses, wallet lists,
  transaction activity, local logs, build output, or bundled runtimes.
- When provider behavior differs, record sanitized evidence in the
  [GMGN contract](Documentation/gmgn-cli-contract.md) and update the affected plan before
  changing integration assumptions. Record the command, versions, failure category,
  output shape, and unresolved behavior when blocked.

## Verification and documentation

Follow [development](Documentation/development.md) for canonical commands.
Build with `/W4`, `/permissive-`, and `/WX` in verification/CI. Tests must cover
thresholds, deduplication, expiry, repeated wallets, freezing, cooldown, retries,
cancellation, and startup baseline. Ordinary tests must not contact GMGN or need credentials;
dependency provisioning/restore may need network access.

Live checks are opt-in. Preserve sanitized fixtures and tested runtime pins when changing
the CLI contract. Record dated evidence in [release validation](Documentation/release-validation.md);
clean Windows 11 x64 acceptance is required before release. Never equate a build or synthetic
soak with live correctness or deployment acceptance.

Keep one authority per topic. Link to code/manifests instead of copying interfaces,
dependency lists, or defaults into multiple guides.
