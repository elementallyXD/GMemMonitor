# GMemMonitor Documentation

This directory is the production documentation home for GMemMonitor. It is the
authoritative record of product scope, engineering decisions, GMGN integration facts,
operational procedures, and delivery status.

## Current status

GMemMonitor is a **Windows WinUI 3 GUI shell plus partial offline core**. It is not yet
an end-to-end monitor. The required GMGN `follow-wallet` feed has not yielded a
sanitized successful fixture. GMGN support reported on 2026-09-08 that
`GET /v1/trade/follow_wallet` is temporarily unavailable on the Free plan and requires
Plus-plan access. No application feature may work around that restriction.

## Documentation map

| Area | Document | Purpose |
|---|---|---|
| Product | [Product brief](Product/product-brief.md) | Approved MVP scope, users, and non-goals. |
| Product | [GMGN capability roadmap](Product/gmgn-capability-roadmap.md) | Confirmed capabilities and future ideas, separated from the MVP. |
| Future study | [Base launch executor plan](Future/base-launch-executor-plan.md) | Separate-product feasibility, threat boundary, safeguards, and delivery gates for an unattended one-shot Base purchase. |
| Architecture | [Architecture](architecture.md) | Component boundaries and runtime data flow. |
| Delivery | [Development plan](Plan.md) | Ordered phase plan; implementation must follow it. |
| Delivery | [Master contract](DevelopmentPlan/00-master-plan.md) | Cross-phase requirements and quality gates. |
| GMGN | [CLI contract](gmgn-cli-contract.md) | Pinned versions, observed behavior, and access limitations. |
| Operations | [GMGN access playbook](Operations/gmgn-access-playbook.md) | Safe configuration, rate-limit, and support procedure. |
| Review | [Production-readiness review](Reviews/production-readiness-review.md) | Current code, security, reliability, and release gaps. |
| Testing | [Human testing](human-testing.md) | Verified local and opt-in live validation steps. |

## Documentation rules

- Record only sanitized GMGN observations. Never add credentials, wallet lists, raw
  responses, transaction hashes, or logs.
- Mark provider claims as time-sensitive and distinguish them from verified local tests.
- Update the CLI contract and active phase plan when GMGN behavior changes.
- Do not describe signals, calls, or social activity as investment advice, safety proof,
  or a profit guarantee.
- Keep future execution/trading ideas outside the read-only MVP until a separate explicit
  product, security, and risk decision exists.
