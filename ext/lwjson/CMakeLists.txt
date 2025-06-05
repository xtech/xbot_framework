include(FetchContent)
FetchContent_Declare(
    lwjson
    URL https://github.com/MaJerle/lwjson/archive/97dfff90b12772d1ec5f4bf4b80cdfe187693423.tar.gz
    PATCH_COMMAND sed -i "s/^cmake_minimum_required.*$/cmake_minimum_required(VERSION 3.16)/" CMakeLists.txt lwjson/CMakeLists.txt

)
FetchContent_MakeAvailable(lwjson)
