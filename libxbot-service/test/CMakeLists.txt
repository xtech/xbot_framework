# Unit tests for xbot-service. Enable with -DXBOT_BUILD_TESTS=ON (the CI preset does), run with ctest.

# The dispatcher tests run against the service generated from the template schema. It uses every
# type the generator knows (blob, string, pooled returns), so this is also what compiles that
# generated code in CI. The name has to match the "type" in the json: the generated source
# includes <type>Base.hpp.
add_service(ServiceTemplate ${XBOT_CODEGEN_PATH}/templates/service.json)
# Generated code ends up in firmware builds that treat warnings as errors.
target_compile_options(ServiceTemplate PRIVATE -Wall -Wextra -Werror)

# QueueTests/QueueTests.cpp is not part of the build: it still uses the queue API from before the
# portable layer (createQueue/destroyQueue) and has to be ported before it can come back.
add_executable(AllTests
        all_tests.cpp
        BufferTests/BufferTests.cpp
        DispatcherTests/DispatcherTests.cpp
        ServiceTests/ServiceTests.cpp
)

target_compile_options(AllTests PRIVATE -Wall -Wextra -Werror)

target_link_libraries(AllTests
        PRIVATE
        ServiceTemplate
        xbot-service
        CppUTest::CppUTest
)

add_test(NAME xbot-service-tests COMMAND AllTests)
# The whole suite runs in milliseconds. A bounds check that regresses shows up as a copy that
# never ends rather than as a failed assertion, so do not wait for the ctest default of 25 minutes.
set_tests_properties(xbot-service-tests PROPERTIES TIMEOUT 60)
