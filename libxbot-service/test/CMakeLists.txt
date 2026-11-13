
add_executable(AllTests
        all_tests.cpp
        QueueTests/QueueTests.cpp
        BufferTests/BufferTests.cpp
        ${PROJECT_SOURCE_DIR}/src/Buffer.cpp
        ${PROJECT_SOURCE_DIR}/src/portable/linux/queue.cpp
)

target_include_directories(AllTests
        PRIVATE
        .
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/../include
)

target_compile_options(AllTests
        PRIVATE $<$<BOOL:${MSVC}>:/wd4723>
)

target_link_libraries(AllTests
        PRIVATE
        CppUTest::CppUTestExt
        ulog
)

if(CPPUTEST_TEST_DISCOVERY OR NOT DEFINED CPPUTEST_TEST_DISCOVERY)
    include(${PROJECT_SOURCE_DIR}/ext/cpputest/cmake/Modules/CppUTest.cmake)
    cpputest_discover_tests(AllTests)
endif()
