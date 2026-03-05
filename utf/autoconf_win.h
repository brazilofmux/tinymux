/* autoconf.h for Windows builds of the utf/ pipeline tools. */

/* Windows does not have iconv */
/* #undef HAVE_ICONV_H */

#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRING_H 1

/* Windows does not have strings.h or unistd.h */
/* #undef HAVE_STRINGS_H */
/* #undef HAVE_UNISTD_H */

#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE__BOOL 1
#define STDC_HEADERS 1

#define PACKAGE_NAME "Tinymux-UTF 2.13"
#define PACKAGE_STRING "Tinymux-UTF 2.13"
#define PACKAGE_TARNAME "tinymux-utf-2-13"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "2.13"
