cmake_minimum_required(VERSION 3.16)
project(codegen C CXX)

# Find Python 3 and create venv
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}" -m venv --system-site-packages "${PROJECT_BINARY_DIR}/.venv")

# Set the virtual environment, tell CMake to look there for python and search again
set(ENV{VIRTUAL_ENV} "${PROJECT_BINARY_DIR}/.venv")
set(Python3_FIND_VIRTUALENV FIRST)
# Need to unset, otherwise search won't be done
unset(Python3_EXECUTABLE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Install requirements into venv
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install -r "${PROJECT_SOURCE_DIR}/requirements.txt")

set(XBOT_CODEGEN_PATH "${PROJECT_SOURCE_DIR}" CACHE PATH "Path to xbot codegen")
set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" CACHE PATH "Path to Pyhton 3 venv")

include(cmake/AddService.cmake)
include(cmake/AddServiceInterface.cmake)
include(cmake/AddFlatBuffersSchema.cmake)
