include(FetchContent)
FetchContent_Declare(flatbuffers URL https://github.com/google/flatbuffers/archive/refs/tags/v25.2.10.tar.gz)
FetchContent_MakeAvailable(flatbuffers)

include(ExternalProject)
ExternalProject_Add(
    flatbuffers-host
    SOURCE_DIR ${flatbuffers_SOURCE_DIR}
    PREFIX ${CMAKE_BINARY_DIR}/_deps/flatbuffers-host
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
               -DFLATBUFFERS_BUILD_FLATLIB=OFF
               -DFLATBUFFERS_BUILD_SHAREDLIB=OFF
               -DFLATBUFFERS_BUILD_TESTS=OFF
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <BINARY_DIR>/flatc
)
