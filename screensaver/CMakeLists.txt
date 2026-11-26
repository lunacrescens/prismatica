# Prismatica's Ripple Tank screensaver: C++17 + OpenGL 3.3 core (4.1 core on macOS).
#   Windows: "Prismatica Ripple Tank.scr"
#   macOS:   "Prismatica Ripple Tank.saver" and "Prismatica Ripple Tank HD.saver"
#   Linux:   a windowed test build (prismatica_saver_test -w)
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# macOS universal (Intel + Apple Silicon):  add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# GLFW is downloaded at configure time. The scene's shaders (src/shaders.h) match electron/shaders.js.
cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
project(PrismaticaScreensaver VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(MSVC)
  # static runtime: the .scr runs without the VC++ redistributable
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------- dependencies (pinned)
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)   # Linux: X11 only (runs under XWayland too)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# ---------------------------------------------------------------- shared settings
function(prismatica_defaults target)
  if(WIN32)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /W3)
  endif()
  if(APPLE)
    target_compile_definitions(${target} PRIVATE GL_SILENCE_DEPRECATION)
  endif()
endfunction()

# ---------------------------------------------------------------- the Ripple Tank screensaver
if(WIN32)
  set(SAVER_NAME "Prismatica Ripple Tank")
  configure_file(res/saver.rc.in ${CMAKE_CURRENT_BINARY_DIR}/saver.rc @ONLY)
  add_executable(prismatica_saver WIN32 saver/saver_glfw.cpp ${CMAKE_CURRENT_BINARY_DIR}/saver.rc)
  prismatica_defaults(prismatica_saver)
  target_compile_definitions(prismatica_saver PRIVATE SAVER_SCENE=7)
  target_include_directories(prismatica_saver PRIVATE src)
  target_link_libraries(prismatica_saver PRIVATE glfw glad)
  set_target_properties(prismatica_saver PROPERTIES OUTPUT_NAME "${SAVER_NAME}" SUFFIX ".scr")
elseif(APPLE)
  # Two savers that can be installed side by side: 1x, and HD (1.5x on Retina screens)
  function(prismatica_mac_saver target class display extra_defs)
    add_library(${target} MODULE saver/saver_mac.mm saver/res/thumbnail.png saver/res/thumbnail@2x.png)
    prismatica_defaults(${target})
    set_source_files_properties(saver/saver_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(saver/res/thumbnail.png saver/res/thumbnail@2x.png PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_compile_definitions(${target} PRIVATE SAVER_SCENE=7 SAVER_CLASS=${class} ${extra_defs})
    target_include_directories(${target} PRIVATE src)
    set(SAVER_DISPLAY_NAME "${display}")
    set(SAVER_CLASS "${class}")
    string(REPLACE " " "" SAVER_ID "${display}")
    configure_file(res/Saver-Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist @ONLY)
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE BUNDLE_EXTENSION saver OUTPUT_NAME "${display}"
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist)
    target_link_libraries(${target} PRIVATE "-framework ScreenSaver" "-framework Cocoa" "-framework OpenGL" "-framework QuartzCore")
  endfunction()
  prismatica_mac_saver(prismatica_saver    PrismaticaRippleTankView   "Prismatica Ripple Tank"    "")
  prismatica_mac_saver(prismatica_saver_hd PrismaticaRippleTankHDView "Prismatica Ripple Tank HD" "SAVER_RETINA;SAVER_SIM_CAP=100000;SAVER_RENDER_SCALE=0.75f")
else()
  # Linux: a windowed test build (run with -w); there is no standard screensaver format to target
  add_executable(prismatica_saver_test saver/saver_glfw.cpp)
  target_compile_definitions(prismatica_saver_test PRIVATE SAVER_SCENE=7)
  target_include_directories(prismatica_saver_test PRIVATE src ${glfw_SOURCE_DIR}/deps)
  find_package(Threads REQUIRED)
  target_link_libraries(prismatica_saver_test PRIVATE glfw glad Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
