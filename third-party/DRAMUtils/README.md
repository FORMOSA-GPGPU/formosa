<img src="https://media.githubusercontent.com/media/tukl-msd/DRAMSys/master/docs/images/dramsys_logo.png" width="350" style="float: left;" alt="DRAMSys Logo"/>



# DRAM Utils (DRAMUtils v1.0)

- [Overview](#overview)
- [Releases](#releases)
- [Usage](#usage)
- [Project structure](#project-structure)
- [Dependencies](#dependencies)
- [Contact Information](#contact-information)

## Overview
DRAMUtils is a small helper library containing classes for DRAM memory specifications (MemSpecs), which is used by [DRAMSys](https://github.com/tukl-msd/DRAMSys) and [DRAMPower](https://github.com/tukl-msd/DRAMPower).

## Releases
The last official release can be found here: https://github.com/tukl-msd/DRAMUtils/releases

The master branch of the repository should be regarded as the bleeding-edge version, which has all the latest features, but also all the latest bugs. Use at your own discretion.

## Usage
DRAMUtils is a header-only library which can be included as-is, or inside a CMake project by being added through add_subdirectory().
The project is also [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) ready and can be easily included in any other CMake based project.

```cmake
include(FetchContent)

FetchContent_Declare(
dramutils
GIT_REPOSITORY https://github.com/tukl-msd/DRAMUtils
GIT_TAG v1.0)
FetchContent_MakeAvailable(dramutils)
```

The library DRAMUtils::DRAMUtils is then available as a target to the rest of the project and can be consumed by any other target, e.g.
```cmake
add_executable(dram_app ${SOURCE_FILES})
target_link_libraries(dram_app PRIVATE DRAMUtils::DRAMUtils)
```
Optionally, test cases can be built by toggling the DRAMUTILS_BUILD_TESTS flag with CMake.

## Project structure
The project is structured in a library part and an (optional) Command Line application.
The library can be built using the CMake target DRAMPower.
Integration of DRAMPower in other projects can be easily achieved by including it as a git submodule or by using the CMake FetchContent directive.

This repository contains the following sub-directoires

     DRAMUtils                  # top directory
     └── cmake                  # cmake scripts used by configuration step
     ├── lib                    # contains bundled dependencies of the project
     ├── include                # top level directory containing the actual sources
         └── DRAMUtils          # source code of the actual DRAMPower library
     └── tests                  # test cases used by the project

## Dependencies
DRAMUtils comes bundled with all necessary libraries (nlohmann_json) and no installation of further system packages is required.

## Contact Information

Further questions about the library or the tools based on DRAMUtils can be directed at:

[DRAMSys@iese.fraunhofer.de](mailto:DRAMSys@iese.fraunhofer.de)

Feel free to ask for updates to the tool's features and please do report any bugs
and errors you encounter. This will encourage us to continuously improve the tool.

## Disclaimer
The user DOES NOT get ANY WARRANTIES when using this library. This software is released under the
BSD 3-Clause License. By using this software, the user implicitly agrees to the
licensing terms.
