# Host unit tests for the ESP-IDF-free modules of components/sensors.
#
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
#
# Unity comes from the ESP-IDF tree: IDF_PATH must be set (it is once the
# ESP-IDF environment is active), or pass -DUNITY_DIR=<dir holding unity.c>.
cmake_minimum_required(VERSION 3.16)
project(kdl_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT UNITY_DIR)
    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR "Set IDF_PATH or pass -DUNITY_DIR=<path to unity/src>")
    endif()
    set(UNITY_DIR "$ENV{IDF_PATH}/components/unity/unity/src")
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(unity STATIC "${UNITY_DIR}/unity.c")
target_include_directories(unity PUBLIC "${UNITY_DIR}")
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

add_library(kdl_sensors STATIC
    "${REPO_ROOT}/components/sensors/pressure_scaling.c"
    "${REPO_ROOT}/components/sensors/tach_math.c"
    "${REPO_ROOT}/components/sensors/sensor_config_parse.c"
    "${REPO_ROOT}/components/data_model/data_model_channels.c")
target_include_directories(kdl_sensors PUBLIC
    "${REPO_ROOT}/components/sensors/include"
    "${REPO_ROOT}/components/data_model/include")
target_compile_options(kdl_sensors PRIVATE -Wall -Wextra -Werror)
target_link_libraries(kdl_sensors PUBLIC m)

enable_testing()
foreach(name test_pressure_scaling test_tach_math test_sensor_config_parse)
    add_executable(${name} "${name}.c")
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_link_libraries(${name} PRIVATE kdl_sensors unity)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
