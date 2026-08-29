# Copyright (c) 2022, Fraunhofer IESE
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
# OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Author:
#   Thomas Psota
#   Marco Mörz
#   Derek Christ

###############################################
###              DRAMUtils 1.0              ###
###############################################
cmake_minimum_required(VERSION 3.22.0)

set(PROJECT_NAME "DRAMUtils")

project(${PROJECT_NAME} VERSION 1.13.4)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

### CMake settings ###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(build_source_group_include)
include(enable_clang_format)
include(enable_clang_tidy)
include(enable_cppcheck)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "" )

if(NOT PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

# Use sane defaults for FetchContent:
# In case we are the top-level project, get everything by default
# In case we are included in another project, the user might want to provide their own system dependencies
option(DRAMUTILS_USE_FETCH_CONTENT "Enable the FetchContent module" ${PROJECT_IS_TOP_LEVEL})
option(DRAMUTILS_USE_FETCH_CONTENT_NLOHMANN_JSON "Enable FetchContent to provide nlohmann json" ${DRAMUTILS_USE_FETCH_CONTENT})

### DRAMUtils directories ###
set(DRAMUTILS_INCLUDE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(DRAMUTILS_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(DRAMUTILS_TEST_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/tests")

### Build options ###
option(DRAMUTILS_BUILD_TESTS "Build DRAMUtils unit tests" ${PROJECT_IS_TOP_LEVEL})
option(DRAMUTILS_INSTALL "Install DRAMUtils" OFF)


### Compiler optimization settings ###
if(PROJECT_IS_TOP_LEVEL)
    option(OPTIMIZE_FOR_NATIVE "Build with -march=native (overrides CPU_TYPE if enabled)" ON)
    set(CPU_TYPE "" CACHE STRING "CPU type for -march=CPU_TYPE and -mtune=CPU_TYPE compile options")
    # Set CPU_TYPE to native if OPTIMIZE_FOR_NATIVE is enabled
    if(OPTIMIZE_FOR_NATIVE)
        if(CPU_TYPE)
            message(NOTICE "OPTIMIZE_FOR_NATIVE is enabled. Overriding CPU_TYPE from \"${CPU_TYPE}\" to \"native\".")
        endif()
        set(CPU_TYPE "native")
    endif()

    # add CPU_TYPE to cmake cxx flags
    if(CPU_TYPE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${CPU_TYPE} -mtune=${CPU_TYPE}")
    endif()
endif()


###############################################
###           Library Settings              ###
###############################################

if (DRAMUTILS_USE_FETCH_CONTENT)
    include(FetchContent)

    if(DRAMUTILS_USE_FETCH_CONTENT_NLOHMANN_JSON)
        FetchContent_Declare(nlohmann_json
            URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
            OVERRIDE_FIND_PACKAGE
        )

        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

find_package(nlohmann_json REQUIRED)

###############################################
###                 DRAMUtils               ###
###############################################

add_library(DRAMUtils INTERFACE)
add_library(DRAMUtils::DRAMUtils ALIAS DRAMUtils)

target_include_directories(DRAMUtils
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(DRAMUtils
    INTERFACE
        nlohmann_json::nlohmann_json
)

target_compile_features(DRAMUtils INTERFACE cxx_std_17)
set_target_properties(DRAMUtils PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(DRAMUtils PROPERTIES CXX_STANDARD_REQUIRED ON)

target_sources(DRAMUtils
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/DRAMUtils/config/toggling_rate.h
            include/DRAMUtils/memspec/MemSpec.h
            include/DRAMUtils/memspec/standards/MemSpecDDR3.h
            include/DRAMUtils/memspec/standards/MemSpecDDR4.h
            include/DRAMUtils/memspec/standards/MemSpecDDR5.h
            include/DRAMUtils/memspec/standards/MemSpecGDDR5.h
            include/DRAMUtils/memspec/standards/MemSpecGDDR5X.h
            include/DRAMUtils/memspec/standards/MemSpecGDDR6.h
            include/DRAMUtils/memspec/standards/MemSpecHBM2.h
            include/DRAMUtils/memspec/standards/MemSpecHBM3_4.h
            include/DRAMUtils/memspec/standards/MemSpecLPDDR4.h
            include/DRAMUtils/memspec/standards/MemSpecLPDDR5.h
            include/DRAMUtils/memspec/standards/MemSpecLPDDR6.h
            include/DRAMUtils/memspec/standards/MemSpecSTTMRAM.h
            include/DRAMUtils/memspec/standards/MemSpecWideIO.h
            include/DRAMUtils/memspec/standards/MemSpecWideIO2.h
            include/DRAMUtils/util/collapsingvector.h
            include/DRAMUtils/util/id_variant.h
            include/DRAMUtils/util/json.h
            include/DRAMUtils/util/json_utils.h
            include/DRAMUtils/util/types.h
)

if (DRAMUTILS_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        TARGETS DRAMUtils
        EXPORT  DRAMUtilsTargets
        FILE_SET HEADERS
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        EXPORT DRAMUtilsTargets
        FILE DRAMUtilsTargets.cmake
        NAMESPACE DRAMUtils::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DRAMUtils
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DRAMUtilsConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/DRAMUtilsConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DRAMUtils
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/DRAMUtilsConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/DRAMUtilsConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/DRAMUtilsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DRAMUtils
    )
endif()

###############################################
###           Test Directory                ###
###############################################

if(DRAMUTILS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

###############################################
###           Utility Projects              ###
###############################################

if(${DRAMUTILS_UTILITY_PROJECTS})
    enable_clang_format()
    enable_clang_tidy()
    enable_cppcheck()
endif()
