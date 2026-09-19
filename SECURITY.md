# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in RIFT, please report it responsibly.

**Do not** open a public GitHub issue for security vulnerabilities.

Instead, email: contact@khalm.ai with the subject line `[Rift Security]`.

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

We will acknowledge receipt within 48 hours and provide a timeline for resolution.

## Scope

This security policy applies to:
- The reference implementation (`reference/`)
- The production binaries (`bin/`)
- The integration test (`tests/`)

This security policy does NOT apply to:
- The training pipeline (proprietary, not distributed)
- Third-party tools or integrations

## Security Design Principles

1. **Zero blast radius** — RIFT has no control path. It cannot drop, redirect, or modify packets.
2. **No dynamic allocation after init** — All mutable state is in a caller-supplied arena.
3. **No floating point in tick path** — Q16.16 fixed-point only. No NaN, no overflow.
4. **eBPF bounded loops only** — Verifier-accepted. No unbounded recursion.
5. **Heartbeat contract** — Stalled heartbeat means RIFT's extra context is lost. The host firewall continues with its own rules.
