cmake_minimum_required(VERSION 3.16)
project(xbot-service C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow logging using ulog
#add_compile_definitions(ULOG_ENABLED)

add_library(xbot-service STATIC
        src/Service.cpp
        src/Lock.cpp
        src/RemoteLogging.cpp
        src/Schedule.cpp
        src/Scheduler.cpp
        src/ServiceIo.cpp
        src/DataSource.cpp
        src/Buffer.cpp
)
target_compile_options(xbot-service PRIVATE -Wall -Wextra -Werror -Wno-volatile)

if (NOT DEFINED XBOT_CUSTOM_PORT_PATH)
    SET(XBOT_CUSTOM_PORT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/portable/linux)
    SET(XBOT_CUSTOM_PORT_LIBS pthread)
else ()
    message("Using custom port at ${XBOT_CUSTOM_PORT_PATH}")
endif ()

file(GLOB_RECURSE PORT_SOURCES ${XBOT_CUSTOM_PORT_PATH}/*.c ${XBOT_CUSTOM_PORT_PATH}/*.cpp ${XBOT_CUSTOM_PORT_PATH}/**/*.c ${XBOT_CUSTOM_PORT_PATH}/**/*.cpp)
target_include_directories(xbot-service PUBLIC ${XBOT_CUSTOM_PORT_PATH}/include)
target_sources(xbot-service PRIVATE ${PORT_SOURCES})


target_include_directories(xbot-service PUBLIC include ../include)

# Shared RPC buffer pool (see include/xbot/config.hpp). The values are compiled into Buffer.cpp and
# into the generated services, so they are PUBLIC: both always see the same pool configuration.
foreach (BUFFER_OPTION XBOT_BUFFER_DEFAULT_POOL XBOT_BUFFER_BLOCK_SIZE XBOT_BUFFER_BLOCK_COUNT)
    if (DEFINED ${BUFFER_OPTION})
        target_compile_definitions(xbot-service PUBLIC ${BUFFER_OPTION}=${${BUFFER_OPTION}})
    endif ()
endforeach ()
target_link_libraries(xbot-service PUBLIC ulog ${XBOT_CUSTOM_PORT_LIBS} heatshrink)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h*"
)
install(TARGETS xbot-service)
#set_property(TARGET xbot-service PROPERTY CXX_STANDARD 23)

# Unit tests, see test/CMakeLists.txt. Off unless asked for: a firmware that adds this directory
# only wants the library.
if (XBOT_BUILD_TESTS)
    add_subdirectory(test)
endif ()
