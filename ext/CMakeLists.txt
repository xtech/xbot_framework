if (XBOT_BUILD_LIB_SERVICE)
    add_subdirectory(ulog/src)
    add_subdirectory(cpputest)
    add_subdirectory(lwjson)
endif ()

if (XBOT_BUILD_LIB_SERVICE_INTERFACE)
    add_subdirectory(json)
    add_subdirectory(spdlog)
    add_subdirectory(crow)
endif ()

add_subdirectory(heatshrink)
