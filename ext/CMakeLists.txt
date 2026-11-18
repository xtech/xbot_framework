if (XBOT_BUILD_LIB_SERVICE)
    add_subdirectory(ulog/src)
    # CppUTest finds leaks by force-including a header that turns `new` and `malloc` into macros.
    # The tests include the service headers and with them <functional> and <thread>, whose
    # placement new does not survive that, so the tests are built without it.
    set(CPPUTEST_MEM_LEAK_DETECTION_DISABLED ON CACHE BOOL "Disable the CppUTest memory leak detection" FORCE)
    add_subdirectory(cpputest)
    add_subdirectory(lwjson)
endif ()

if (XBOT_BUILD_LIB_SERVICE_INTERFACE)
    add_subdirectory(json)
    add_subdirectory(spdlog)
    add_subdirectory(crow)
endif ()

add_subdirectory(heatshrink)
