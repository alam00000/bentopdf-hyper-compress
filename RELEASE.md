# Releasing

One command:

```bash
scripts/release.sh patch     # or minor, major, or an explicit X.Y.Z
```

The script refuses to run unless you are on a clean, up-to-date main. It bumps
`HPDF_VERSION` in `sdk/native/hpdf.h` (the source of truth), stamps
`package.json`, every package under `packages/npm/`, the root
`optionalDependencies` pins and the lockfile, runs `make check`, commits
`chore: release vX.Y.Z`, tags, and pushes. `DRY_RUN=1 scripts/release.sh patch`
shows what would happen without changing anything.

CI does the rest, in order:

1. `release.yml` builds, on real runners from the pinned PDFium checkout:
   - `hyper-compress-<tag>-macos-arm64.tar.gz`: hpdf-worker, hpdf-render, qpdf
     (stripped and signed by `scripts/harden.sh`)
   - `hyper-compress-<tag>-linux-x64.tar.gz`: same three binaries
   - `hyper-compress-<tag>-windows-x64.tar.gz`: hpdf-worker.exe,
     hpdf-render.exe, and qpdf.exe with its runtime DLLs (fetched
     checksum-pinned from the official qpdf release)
   - `hyper-compress-<tag>-wasm.tar.gz`: the WebAssembly engine module
   - `libhypercompress-<tag>-macos-arm64.tar.gz`: the C API dylib
   - the docker image, pushed to `ghcr.io/alam00000/bentopdf-hyper-compress`
     as `<tag>` and `latest`
2. It publishes a GitHub Release with a `SHA256SUMS` file covering every
   artifact. The Windows job is allowed to fail without blocking the release;
   macOS and Linux are required.
3. `npm-publish.yml` starts when `release.yml` finishes successfully. It stamps
   every package version from the tag, fills the platform packages from the
   release tarballs, assembles `hyper-compress-wasm` from the wasm tarball, and
   publishes `hyper-compress`, `hyper-compress-wasm`,
   `hyper-compress-darwin-arm64`, `hyper-compress-linux-x64` and
   `hyper-compress-win32-x64` with provenance. A platform whose tarball is
   missing is skipped with a log line instead of aborting the publish; it
   ships in the next release.

   It runs as its own workflow rather than a reusable one called by
   `release.yml` on purpose: npm's trusted publishing validates the OIDC claim
   against the calling workflow, so a `workflow_call` chain would check
   `release.yml` and never match the publisher configured for
   `npm-publish.yml`. To republish a tag whose packages did not reach npm, run
   the `npm-publish` workflow manually with that tag.

After the release, the nightly workflow starts exercising the new binaries:
it downloads them, verifies checksums, and runs `make check` plus
`make regress` against them every night.

## Verifying a release

```bash
gh release download v0.2.0 --pattern 'SHA256SUMS' --pattern '*macos-arm64*'
shasum -a 256 -c SHA256SUMS --ignore-missing
```
