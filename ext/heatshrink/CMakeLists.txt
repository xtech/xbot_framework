include(FetchContent)

# Use "develop" branch
FetchContent_Declare(heatshrink URL https://github.com/atomicobject/heatshrink/archive/e4084caa5c570c5e733de6b12ee1c9000c52d1b4.tar.gz)
FetchContent_MakeAvailable(heatshrink)

add_library(heatshrink STATIC
    ${heatshrink_SOURCE_DIR}/src/heatshrink_decoder.c
    ${heatshrink_SOURCE_DIR}/src/heatshrink_encoder.c
)

target_compile_definitions(heatshrink PUBLIC
    HEATSHRINK_DYNAMIC_ALLOC=0
    HEATSHRINK_STATIC_WINDOW_BITS=9
    HEATSHRINK_STATIC_LOOKAHEAD_BITS=5
)

target_include_directories(heatshrink
    PUBLIC
        ${heatshrink_SOURCE_DIR}/include
        ${heatshrink_SOURCE_DIR}/src
)
