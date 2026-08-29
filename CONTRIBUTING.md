# Contributing

Thanks for wanting to help. Two things to know before your first PR:

1. **CLA.** BentoPDF uses a dual licensing model (AGPL-3.0 plus a commercial
   license), so we need a signed [Contributor License Agreement](ICLA.md)
   before we can merge anything. The CLA bot will prompt you on your first
   pull request; signing is a single comment and only happens once. Corporate
   contributors: see [CCLA.md](CCLA.md).
2. **Bug fixes come with their PDF.** The regression suite is a directory of
   documents that each broke the engine once. If you fix a bug, add the
   reproducing file to `tests/regression/failset/` so it stays fixed.

## Getting set up

```bash
npm ci
make check        # typecheck, lint, unit tests; works on a bare checkout
```

That's enough for changes to the SDK, CLI, tests, or web code. For engine
(C++) work you need built worker binaries: either grab them from the latest
release into `cli/prebuilt/`, or build from source per [BUILDING.md](BUILDING.md).
With binaries present, `make check` also runs the integration tests, and:

```bash
make regress      # the failset, every preset, validity + fidelity + text layer
```

is the gate a bug fix has to pass; it ends with a failing-pairs count that
must be zero.

## Layout

- `core/src` is the engine, one big translation unit plus small wrappers.
- `core/patches` are full-file patches against the pinned PDFium revision.
  `core/build/apply-tree.sh` owns the pin; never edit the checkout directly.
- `sdk/node`, `sdk/native`, `cli`, `wasm`, `web`, and `server` are all front
  ends over the same engine. Behavior belongs in the engine; front ends stay
  thin.

## Rules of the road

- The C ABI (`sdk/native/hpdf.h`) is additive-only within a major version.
  `HPDF_VERSION` there is the single version source; `make check` fails if
  package.json drifts from it.
- The output must never grow and never break: anything that can degrade a
  document has to be guarded by verification or a rollback, like the existing
  regeneration and JBIG2 checks.
- Fuzzing runs through ClusterFuzzLite on every pull request that touches the
  engine, and again nightly for a longer session. `make fuzz` runs the same
  harness locally. If your change touches parsing paths, run the fuzzer for a
  while before opening the PR.
- No comments policy: the code in this repository is deliberately
  comment-free; write code that doesn't need them.
