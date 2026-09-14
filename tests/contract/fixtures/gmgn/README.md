# GMGN Fixture Rules

This directory intentionally contains no live response data yet.

Phase 01 may add fixtures only after sanitization preserves JSON structure and value types while replacing:

- API keys, signatures, and request credentials.
- GMGN record IDs and transaction hashes.
- Followed wallet and token addresses.
- Wallet names, labels, symbols, URLs, and account-specific metadata.
- Any diagnostic text that could reveal account or network information.

Keep only the fields needed for parser/contract tests. Name fixtures by response shape, for example:

```text
follow_wallet_buy.json
follow_wallet_empty.json
token_info_bsc.json
token_security_bsc.json
error_429.json
```

`GmgnContractProbe --sanitized-fixture` is the approved capture path. It preserves
JSON object keys and value types while replacing every response value in memory before
creating a new file directly in this directory. Review its output before committing:
JSON keys must be stable API-contract names, not user-controlled content.

Never capture raw account responses with shell redirection or commit a copied terminal transcript.
