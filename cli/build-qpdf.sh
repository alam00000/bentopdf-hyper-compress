#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

QPDF_VERSION="${QPDF_VERSION:-12.3.2}"
SHA256_TARBALL="6cba2f9f2cd887d905faeb99e0e51a307b217920d1bbf3e9cfbb2e8178a2deda"
WORK="${WORK:-$HERE/build-work}"
OS="$(uname -s)"

if [[ "$OS" == "Darwin" ]]; then
  JPEG_TURBO="${JPEG_TURBO:-/opt/homebrew/opt/jpeg-turbo}"
  JPEG_INCLUDE="$JPEG_TURBO/include"
  JPEG_LIB="$JPEG_TURBO/lib/libjpeg.a"
  NCPU="$(sysctl -n hw.ncpu)"
else
  JPEG_INCLUDE="${JPEG_INCLUDE:-/usr/include}"
  JPEG_LIB="${JPEG_LIB:-$(ls /usr/lib/*/libjpeg.a /usr/lib/libjpeg.a 2>/dev/null | head -1 || true)}"
  NCPU="$(nproc)"
fi

if [[ -z "$JPEG_LIB" || ! -f "$JPEG_LIB" ]]; then
  echo "ERROR: static libjpeg-turbo not found (looked for ${JPEG_LIB:-libjpeg.a})" >&2
  if [[ "$OS" == "Darwin" ]]; then
    echo "  brew install jpeg-turbo" >&2
  else
    echo "  apt-get install libjpeg-dev" >&2
  fi
  exit 1
fi

mkdir -p "$WORK"
cd "$WORK"

TARBALL="qpdf-$QPDF_VERSION.tar.gz"
if [[ ! -f "$TARBALL" ]]; then
  echo "==> Downloading qpdf $QPDF_VERSION"
  curl -fsSL -o "$TARBALL" \
    "https://github.com/qpdf/qpdf/releases/download/v$QPDF_VERSION/$TARBALL"
fi
if command -v shasum >/dev/null 2>&1; then
  echo "$SHA256_TARBALL  $TARBALL" | shasum -a 256 -c -
else
  echo "$SHA256_TARBALL  $TARBALL" | sha256sum -c -
fi

rm -rf "qpdf-$QPDF_VERSION"
tar xzf "$TARBALL"

ARCH="${HYPER_TARGET_ARCH:-$(uname -m)}"
case "$ARCH" in x86_64) ARCH=x64 ;; aarch64) ARCH=arm64 ;; esac
CLANG_ARCH=$([[ "$ARCH" == x64 ]] && echo x86_64 || echo arm64)

EXTRA_CMAKE=()
if [[ "$OS" == "Darwin" ]]; then
  EXTRA_CMAKE+=(-DCMAKE_OSX_ARCHITECTURES="$CLANG_ARCH")
else
  EXTRA_CMAKE+=(-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
fi

echo "==> Configuring qpdf $QPDF_VERSION ($OS $ARCH)"
cmake -S "qpdf-$QPDF_VERSION" -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DUSE_IMPLICIT_CRYPTO=OFF \
  -DREQUIRE_CRYPTO_NATIVE=ON \
  -DDEFAULT_CRYPTO=native \
  -DBUILD_DOC=OFF \
  -DBUILD_DOC_PDF=OFF \
  -DBUILD_TESTING=OFF \
  -DPKG_CONFIG_EXECUTABLE=/usr/bin/false \
  -DLIBJPEG_H_PATH="$JPEG_INCLUDE" \
  -DLIBJPEG_LIB_PATH="$JPEG_LIB" \
  "${EXTRA_CMAKE[@]}" \
  >/dev/null

echo "==> Building"
cmake --build build --target qpdf -j "$NCPU" >/dev/null

BIN="build/qpdf/qpdf"
[[ -f "$BIN" ]] || BIN="$(find build -name qpdf -type f -perm -u+x | head -1)"
[[ -f "$BIN" ]] || { echo "ERROR: built qpdf not found" >&2; exit 1; }

echo "==> Verifying self-containment (system libraries only)"
if [[ "$OS" == "Darwin" ]]; then
  if otool -L "$BIN" | tail -n +2 | grep -vE '^\s+/usr/lib/'; then
    echo "ERROR: non-system dylib dependency listed above -- not self-contained" >&2
    exit 1
  fi
else
  if ldd "$BIN" | grep -vE 'linux-vdso|ld-linux|=> /(usr/)?lib|^\s+/(usr/)?lib'; then
    echo "ERROR: non-system library dependency listed above -- not self-contained" >&2
    exit 1
  fi
fi

strip -S -x "$BIN" 2>/dev/null || true
mkdir -p "$HERE/prebuilt"
cp -f "$BIN" "$HERE/prebuilt/qpdf"
chmod 755 "$HERE/prebuilt/qpdf"
if [[ "$OS" == "Darwin" ]]; then
  codesign -f -s - "$HERE/prebuilt/qpdf"
fi

echo "==> Done: $HERE/prebuilt/qpdf"
"$HERE/prebuilt/qpdf" --version | head -1
