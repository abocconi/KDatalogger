# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(KDatalogger)

# Copy of the app image named after its version (version.txt): the file an
# operator drops on the USB volume to update the firmware (components/fw_update).
idf_build_get_property(kdl_build_dir BUILD_DIR)
idf_build_get_property(kdl_project_bin PROJECT_BIN)
idf_build_get_property(kdl_project_ver PROJECT_VER)
set(kdl_release_bin "${CMAKE_PROJECT_NAME}_v${kdl_project_ver}.bin")
add_custom_target(release_bin ALL
    COMMAND ${CMAKE_COMMAND} -E copy "${kdl_build_dir}/${kdl_project_bin}"
            "${kdl_build_dir}/${kdl_release_bin}"
    COMMENT "Update image: build/${kdl_release_bin}"
    VERBATIM)
add_dependencies(release_bin gen_project_binary)
