/*
 * Hyper overlay: stand-in for leptonica's autoconf-generated
 * endianness.h.  Upstream's configure.ac produces this file at
 * build time; pdfium's GN build doesn't run autoconf, so we ship
 * a static version that picks the right macro from the compiler's
 * built-in endian probes.  Works on every arm64 / x86_64 macOS /
 * Linux toolchain we care about; the Apple-universal branch is
 * left in for the same multi-arch coverage upstream provides.
 */
#if !defined(L_BIG_ENDIAN) && !defined(L_LITTLE_ENDIAN)
#  if defined(__APPLE__) && defined(__BIG_ENDIAN__)
#    define L_BIG_ENDIAN
#  elif defined(__APPLE__) && defined(__LITTLE_ENDIAN__)
#    define L_LITTLE_ENDIAN
#  elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    define L_BIG_ENDIAN
#  else
#    define L_LITTLE_ENDIAN
#  endif
#endif
