cmake_minimum_required(VERSION 3.18)
project(atlaslight VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# -mtune only changes instruction scheduling; the binary still runs on any i686 CPU.
option(ATLAS_TUNE_ATOM "Tune code for Intel Atom (Packard Bell Dot, N2600)" ON)
if(ATLAS_TUNE_ATOM AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-mtune=bonnell)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(CURL REQUIRED libcurl)

# ---- litehtml (cloned into third_party/litehtml by the workflow / README steps) ----
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/litehtml/CMakeLists.txt)
  message(FATAL_ERROR
    "third_party/litehtml is missing. Run:\n"
    "  git clone https://github.com/litehtml/litehtml third_party/litehtml\n"
    "  git -C third_party/litehtml checkout ebb9d32b28cb8c0f6dff2257c1ace676da0dc4b1")
endif()
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LITEHTML_BUILD_CONTAINER_PANGO ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)

# ---- QuickJS (vendored in third_party/quickjs, MIT licence, from quickjs-ng) ----
option(ATLAS_ENABLE_JS "Build browser scripting with the bundled QuickJS" ON)
if(ATLAS_ENABLE_JS)
  add_library(qjs STATIC
    third_party/quickjs/dtoa.c
    third_party/quickjs/libregexp.c
    third_party/quickjs/libunicode.c
    third_party/quickjs/quickjs.c)
  target_compile_definitions(qjs PRIVATE _GNU_SOURCE)
  target_include_directories(qjs PUBLIC third_party/quickjs)
  set_target_properties(qjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_compile_options(qjs PRIVATE -w) # vendored code: do not spam warnings
  find_library(ATOMIC_LIBRARY atomic) # 64-bit atomics on some 32-bit toolchains
  target_link_libraries(qjs PUBLIC m ${CMAKE_DL_LIBS} Threads::Threads)
  if(ATOMIC_LIBRARY)
    target_link_libraries(qjs PUBLIC ${ATOMIC_LIBRARY})
  endif()
  message(STATUS "QuickJS (bundled): user scripting ENABLED")
else()
  message(STATUS "User scripting disabled (ATLAS_ENABLE_JS=OFF)")
endif()

# ---- vendored network layer (from litehtml support/webpage, see src/net/NOTICE.md) ----
add_library(atlas_net STATIC
  src/net/web_page.cpp
  src/net/web_history.cpp
  src/net/http_request.cpp
  src/net/http_requests_pool.cpp)
target_include_directories(atlas_net PUBLIC src/net ${CURL_INCLUDE_DIRS})
target_link_libraries(atlas_net PUBLIC litehtml_container_pango ${CURL_LIBRARIES} Threads::Threads)

# ---- ad blocker (no dependencies) ----
add_library(atlas_adblock STATIC src/adblock.cpp)
target_include_directories(atlas_adblock PUBLIC src)

# ---- scripting ----
add_library(atlas_js STATIC src/js/js_engine.cpp)
target_include_directories(atlas_js PUBLIC src)
if(ATLAS_ENABLE_JS)
  target_compile_definitions(atlas_js PRIVATE ATLAS_HAVE_QUICKJS=1)
  target_link_libraries(atlas_js PUBLIC qjs)
endif()

# ---- the browser ----
add_executable(atlaslight src/main.cpp)
target_include_directories(atlaslight PRIVATE src ${GTK3_INCLUDE_DIRS})
target_compile_options(atlaslight PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_directories(atlaslight PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_libraries(atlaslight PRIVATE atlas_net atlas_adblock atlas_js ${GTK3_LIBRARIES})

# ---- tests ----
enable_testing()
add_executable(adblock_test tests/adblock_test.cpp)
target_link_libraries(adblock_test PRIVATE atlas_adblock)
add_test(NAME adblock COMMAND adblock_test)
add_executable(js_test tests/js_test.cpp)
target_link_libraries(js_test PRIVATE atlas_js)
add_test(NAME scripting COMMAND js_test)

# ---- install + .deb ----
install(TARGETS atlaslight RUNTIME DESTINATION bin)
install(FILES data/blocklist.txt DESTINATION share/atlaslight)
install(FILES data/atlaslight.desktop DESTINATION share/applications)
install(FILES data/example-script.js DESTINATION share/atlaslight)
install(FILES third_party/quickjs/LICENSE DESTINATION share/doc/atlaslight RENAME LICENSE-quickjs)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "atlaslight")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "AtlasLight authors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tiny GTK3 web browser for old netbooks")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
