/* options */
/* #undef AVOID_WINDOWS_HANDLE */
#define DEFAULT_CRYPTO "native"
/* #undef USE_CRYPTO_GNUTLS */
#define USE_CRYPTO_NATIVE 1
/* #undef USE_CRYPTO_OPENSSL */
/* #undef USE_INSECURE_RANDOM */
/* #undef SKIP_OS_SECURE_RANDOM */
/* #undef ZOPFLI */

/* large file support -- may be needed for 32-bit systems */
/* #undef _FILE_OFFSET_BITS */

/* headers files */
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1

/* OS functions and symbols */
#define HAVE_EXTERN_LONG_TIMEZONE 1
#define HAVE_FSEEKO 1
/* #undef HAVE_FSEEKO64 */
#define HAVE_LOCALTIME_R 1
#define HAVE_RANDOM 1
/* #undef HAVE_TM_GMTOFF */
/* #undef HAVE_MALLOC_INFO */
#define HAVE_OPEN_MEMSTREAM 1

/* bytes in the size_t type */
#define SIZEOF_SIZE_T 4
