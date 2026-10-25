add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/freetype)
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/imgwz)

# Carried in-tree rather than fetched, because the client's windowing needs edits inside glfm's
# Android backend that upstream does not have: the AKEYCODE_BUTTON_* gamepad mapping and its axis
# hook, and the deadline in the render loop that holds the frame callback to a requested rate.
#
# The tree is brackeen/glfm at 607fc6ff8c89d58641c79adfb00ac9ced3652adc with those changes applied,
# so a diff against upstream at that tag shows exactly what is ours. examples/ and tests/ are inert:
# they sit behind GLFM_BUILD_EXAMPLES and GLFM_USE_CLANG_TIDY, both off.
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/glfm)

# Nothing here is downloaded at configure time: freetype, imgwz, glfm, stb, asio and bass are all in
# this directory, so a clone builds on its own.
#
# stb is only two files - the header the client includes and the single translation unit that is
# compiled into the app - from nothings/stb at b42009b3b9d4ca35bc703f5310eedc74f584be58, unmodified,
# plus its license. It has no target to add; see the stb include and source paths in the parent
# CMakeLists.txt.
