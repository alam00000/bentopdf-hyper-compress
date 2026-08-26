# Releasing

Releases are cut by pushing a tag; `.github/workflows/release.yml` does the
rest.

```bash
# 1. bump the version: sdk/native/hpdf.h HPDF_VERSION is the source of truth,
#    package.json must match (make check enforces it)
# 2. make check && make regress
git tag v0.2.0
git push origin v0.2.0
```

The workflow then builds, on real runners from the pinned PDFium checkout:

- `hyper-compress-<tag>-macos-arm64.tar.gz`: hpdf-worker, hpdf-render, qpdf
  (stripped and signed by `scripts/harden.sh`)
- `hyper-compress-<tag>-linux-x64.tar.gz`: same three binaries
- `hyper-compress-<tag>-windows-x64.tar.gz`: hpdf-worker.exe, hpdf-render.exe,
  and qpdf.exe with its runtime DLLs (fetched checksum-pinned from the
  official qpdf release)
- `hyper-compress-<tag>-wasm.tar.gz`: the WebAssembly engine module
- `libhypercompress-<tag>-macos-arm64.tar.gz`: the C API dylib
- the docker image, pushed to `ghcr.io/alam00000/bentopdf-hyper-compress`
  as `<tag>` and `latest`

and publishes a GitHub Release with a `SHA256SUMS` file covering every
artifact. The Windows job is allowed to fail without blocking the release;
macOS and Linux are required.

After the release, the nightly workflow starts exercising the new binaries:
it downloads them, verifies checksums, and runs `make check` plus
`make regress` against them every night.

## npm

Publishing the GitHub Release triggers `.github/workflows/npm-publish.yml`,
which stamps every package version from the tag (including the platform
packages and the root `optionalDependencies` pins), fills the platform
packages from the release tarballs, assembles `hyper-compress-wasm`
from the wasm tarball, and publishes whatever is complete with npm trusted
publishing and provenance. A platform whose tarball is missing (for example a
failed Windows build) is skipped with a log line instead of aborting the
publish; it can ship in the next release.

## Verifying a release

```bash
gh release download v0.2.0 --pattern 'SHA256SUMS' --pattern '*macos-arm64*'
shasum -a 256 -c SHA256SUMS --ignore-missing
```
