# Contributing

Thank you for your interest in RIFT.

## What is open to contributions

The reference implementation (`reference/`), the documentation (`docs/`),
and the verification scripts (`tests/`) are Apache 2.0. Patches to them are
welcome, under the same license, with no CLA.

The production runtime is **not open to code contributions** in this
repository. Bug reports against the production binaries are welcome and
valued; patches to the production runtime are not accepted publicly.

## Reporting a bug

Open an issue with:

1. What you ran (command line, verbatim)
2. What you expected
3. What happened
4. Your environment:
   ```
   uname -a
   cat /etc/os-release
   sha256sum data/reflex-brain-rift-demo.rift
   ```

## Proposing a change to the reference implementation

- Keep it correct, not fast. The reference is deliberately unoptimized.
- If the change alters arithmetic, the canonical replay hash
  (`data/reflex-brain-rift-demo-replay.sha256`) must still match. CI
  enforces this; a change that breaks byte-identity will not merge.
- Update `reference/README.md` or the relevant doc if behavior changes.
- Add a test if the change is non-trivial.

## Pull request checklist

- CI is green (reference build + canonical hash check)
- No new dependencies beyond a C++11 compiler and the standard library
- No production-only code in the reference implementation

## Security issues

Do not open issues for security vulnerabilities. See `SECURITY.md`.
