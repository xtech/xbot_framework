if (XBOT_BUILD_LIB_SERVICE)
    add_subdirectory(ulog/src)
    add_subdirectory(cpputest)
endif ()

if (XBOT_BUILD_LIB_SERVICE_INTERFACE)
    add_subdirectory(json)
    add_subdirectory(spdlog)
    add_subdirectory(crow)
endif ()

if (XBOT_BUILD_LIB_SERVICE OR XBOT_BUILD_LIB_SERVICE_INTERFACE)
    add_library(zcbor EXCLUDE_FROM_ALL
            zcbor/src/zcbor_common.c
            zcbor/src/zcbor_decode.c
            zcbor/src/zcbor_encode.c
     )
    target_include_directories(zcbor PUBLIC zcbor/include)
endif ()
