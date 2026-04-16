cmake_minimum_required(VERSION 3.20)

project(vscpl1drv-can4vscp VERSION 1.1.1 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DRIVER_TARGET vscpl1drv-can4vscp)
set(DRIVER_SOVERSION 1)

set(VSCP_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vscp/src/common)
set(VSCP_VSCP_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vscp/src/vscp/common)
set(DRIVER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(DRIVER_SOURCES
    src/common/can4vscpobj.cpp
    ${VSCP_COMMON_DIR}/crc8.c
    ${VSCP_COMMON_DIR}/dllist.c
)

if(WIN32)
    list(APPEND DRIVER_SOURCES
        src/windows/can4vscpdrv.cpp
        src/common/dlldrvobj.cpp
        ${VSCP_COMMON_DIR}/com_win32.cpp
    )
else()
    list(APPEND DRIVER_SOURCES
        src/linux/vscpl1drv-can4vscp.cpp
        ${VSCP_COMMON_DIR}/com.cpp
    )
endif()

add_library(${DRIVER_TARGET} SHARED ${DRIVER_SOURCES})

target_include_directories(${DRIVER_TARGET}
    PRIVATE
    ${DRIVER_SOURCE_DIR}/common
    ${DRIVER_SOURCE_DIR}/linux
    ${DRIVER_SOURCE_DIR}/windows
        ${VSCP_COMMON_DIR}
        ${VSCP_VSCP_COMMON_DIR}
)

target_compile_definitions(${DRIVER_TARGET} PRIVATE _REENTRANT)

if(WIN32)
    target_compile_definitions(${DRIVER_TARGET} PRIVATE WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS CAN4VSCPDRV_EXPORTS)
    target_link_libraries(${DRIVER_TARGET} PRIVATE odbc32 odbccp32 ws2_32)
else()
    target_link_libraries(${DRIVER_TARGET} PRIVATE m pthread dl)
endif()

set_target_properties(${DRIVER_TARGET} PROPERTIES
    OUTPUT_NAME ${DRIVER_TARGET}
    PREFIX ""
    VERSION ${PROJECT_VERSION}
    SOVERSION ${DRIVER_SOVERSION}
)

if(WIN32)
    install(TARGETS ${DRIVER_TARGET}
        RUNTIME DESTINATION drivers/level1
        LIBRARY DESTINATION drivers/level1
    )
else()
    install(TARGETS ${DRIVER_TARGET}
        LIBRARY DESTINATION var/lib/vscp/drivers/level1
    )

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/man/vscpl1drv-can4vscp.1)
        install(FILES man/vscpl1drv-can4vscp.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
endif()

set(CPACK_PACKAGE_NAME "vscpl1drv-can4vscp")
set(CPACK_PACKAGE_VENDOR "Grodans Paradis AB")
set(CPACK_PACKAGE_CONTACT "Ake Hedman <akhe@grodansparadis.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VSCP Level I CAN4VSCP Serial Driver")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/vscp/LICENSE)
set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ake Hedman")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "VSCP L1 Driver CAN4VSCP")
    set(CPACK_NSIS_PACKAGE_NAME "vscpl1drv-can4vscp")
    set(CPACK_NSIS_CONTACT "akhe@grodansparadis.com")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

include(CPack)