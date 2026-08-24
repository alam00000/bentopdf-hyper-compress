### Description

Please include a summary of the change and which issue is fixed, with relevant
motivation and context.

Fixes # (issue)

### Type of change

Please delete options that are not relevant.

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] This change requires a documentation update

### How has this been tested?

- [ ] `make check` passes (typecheck, lint, tests, version lock)
- [ ] `make regress` passes (needs built worker binaries)
- [ ] For a bug fix: the reproducing PDF is added to `tests/regression/failset/`
- [ ] For native/engine changes: rebuilt and verified locally per BUILDING.md

### Checklist

- [ ] I have signed the CLA (the bot will prompt on your first PR)
- [ ] The C ABI (`sdk/native/hpdf.h`) is unchanged, or the change is additive
- [ ] `CHANGELOG`-worthy behavior changes are described above
