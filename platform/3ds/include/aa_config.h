#ifndef ARMAGETRON_AA_CONFIG_H
#define ARMAGETRON_AA_CONFIG_H

#define __3DS__ 1
#define ENABLE_ZONESV2 1
#define HAVE_ARPA_INET_H 1
#define HAVE_COSF 1
#define HAVE_DIRENT_H 1
#define HAVE_EXPF 1
#define HAVE_FABSF 1
#define HAVE_FLOORF 1
#define HAVE_LOGF 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_PTHREAD 1
#define HAVE_PTHREAD_H 1
#define HAVE_SINF 1
#define HAVE_SQRTF 1
#define HAVE_SOCKLEN_T 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TANF 1
#define HAVE_UNISTD_H 1
#define HAVE_WMEMSET 1

#define PACKAGE "armagetronad"
#define PACKAGE_NAME "Armagetron Advanced"
#define PACKAGE_VERSION "3ds-port"
#define VERSION "3ds-port"

#define DATA_DIR "romfs:"
#define USER_DATA_DIR "sdmc:/3ds/armagetronad"
#define USER_CONFIG_DIR "sdmc:/3ds/armagetronad/config"
#define SCREENSHOT_DIR "sdmc:/3ds/armagetronad/screenshots"
#define VAR_DIR "sdmc:/3ds/armagetronad/var"
#define AUTORESOURCE_DIR "sdmc:/3ds/armagetronad/resource/automatic"
#define INCLUDEDRESOURCE_DIR "romfs:/resource/included"

#define AA_DATADIR DATA_DIR
#define AA_SYSCONFDIR "romfs:/config"

#endif

