include(FetchContent)
FetchContent_Declare(
    flatcc
    URL https://github.com/dvidelabs/flatcc/archive/810821ea5bc87ac7b874a703d354ea4d75d83a06.tar.gz
    PATCH_COMMAND patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/patch.diff
)
FetchContent_MakeAvailable(flatcc)
set(FLATCC_RTONLY on)

include(ExternalProject)
ExternalProject_Add(
    flatcc-host
    SOURCE_DIR ${flatcc_SOURCE_DIR}
    PREFIX ${CMAKE_BINARY_DIR}/_deps/flatcc-host
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
               -DFLATCC_TEST=OFF
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <BINARY_DIR>/flatc
)
