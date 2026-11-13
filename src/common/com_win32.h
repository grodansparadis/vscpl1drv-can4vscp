#ifndef VSCP_COM_WIN32_UNDERSCORE_COMPAT_H
#define VSCP_COM_WIN32_UNDERSCORE_COMPAT_H

// The VSCP sources are fetched by CMake; it passes the header's location
#ifndef VSCP_COM_WIN32_HEADER
#error "VSCP_COM_WIN32_HEADER is not defined (it is set in CMakeLists.txt)"
#endif
#include VSCP_COM_WIN32_HEADER

#endif // VSCP_COM_WIN32_UNDERSCORE_COMPAT_H
