# dependencies.cmake
#
# Third-party sources, fetched at configure time with FetchContent. They used
# to be git submodules in third-party/; each is pinned to the same commit.
# Every download is checked against its SHA256.
#
# To use a local checkout instead, set FETCHCONTENT_SOURCE_DIR_<NAME>, for
# example -DFETCHCONTENT_SOURCE_DIR_TP_VSCP=/path/to/vscp.
# FETCHCONTENT_FULLY_DISCONNECTED=ON reuses downloaded sources offline.
#
# Sets:
#   THIRD_PARTY_VSCP_DIR               grodansparadis/vscp
#   THIRD_PARTY_SPDLOG_DIR             gabime/spdlog
#

include_guard(GLOBAL)
include(FetchContent)

set(_tp_options)
if(NOT CMAKE_VERSION VERSION_LESS "3.18")
    # Populate the sources only; the projects are not added to the build.
    list(APPEND _tp_options SOURCE_SUBDIR "__sources_only__")
endif()
if(NOT CMAKE_VERSION VERSION_LESS "3.24")
    list(APPEND _tp_options DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

# grodansparadis/vscp @ 14f04a9962dede9082f739933d710f62b39aa39d
FetchContent_Declare(
    tp_vscp
    URL https://codeload.github.com/grodansparadis/vscp/tar.gz/14f04a9962dede9082f739933d710f62b39aa39d
    URL_HASH SHA256=0d7c5555a003d2de83f5bee55e3f2203bdaea201bbc42edf27cf77ccc7b0c586
    ${_tp_options}
)
# gabime/spdlog @ 57cb5fb7a8ff30079751728234623230535a5c92
FetchContent_Declare(
    tp_spdlog
    URL https://codeload.github.com/gabime/spdlog/tar.gz/57cb5fb7a8ff30079751728234623230535a5c92
    URL_HASH SHA256=e7a5ba64c021d0889e31d9a03b9296b5db1663c28293cf8a7ea173abd66bcd0c
    ${_tp_options}
)

set(_tp_names tp_vscp tp_spdlog)
if(CMAKE_VERSION VERSION_LESS "3.18")
    foreach(_tp IN LISTS _tp_names)
        FetchContent_GetProperties(${_tp})
        if(NOT ${_tp}_POPULATED)
            FetchContent_Populate(${_tp})
        endif()
    endforeach()
else()
    FetchContent_MakeAvailable(${_tp_names})
endif()

set(THIRD_PARTY_VSCP_DIR "${tp_vscp_SOURCE_DIR}")
set(THIRD_PARTY_SPDLOG_DIR "${tp_spdlog_SOURCE_DIR}")
