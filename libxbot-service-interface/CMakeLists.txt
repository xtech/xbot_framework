cmake_minimum_required(VERSION 3.16)
project(xbot-service-interface C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(xbot-service-interface
        src/Socket.cpp
        include/xbot-service-interface/ServiceInterfaceBase.hpp
        src/ServiceInterfaceBase.cpp
        include/xbot-service-interface/XbotServiceInterface.hpp
        src/ServiceDiscoveryImpl.cpp
        src/ServiceIO.cpp
        src/PlotJugglerBridge.cpp
        src/ServiceIOImpl.hpp
        src/XbotServiceInterface.cpp
        src/RemoteLoggingReceiverImpl.cpp
)

target_include_directories(xbot-service-interface PUBLIC
        include
        ../include
)
target_link_libraries(xbot-service-interface PUBLIC
        nlohmann_json::nlohmann_json spdlog::spdlog Crow::Crow
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h*"
)
install(TARGETS xbot-service-interface)
