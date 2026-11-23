cmake_minimum_required(VERSION 3.21)
project(towel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
find_package(KF6SyntaxHighlighting REQUIRED)

# libgit2: its CMake package, else pkg-config, else a plain library search (Windows)
option(TOWEL_STATIC_GIT2 "libgit2 is a static library (link its private dependencies too)" OFF)
find_package(libgit2 CONFIG QUIET)
find_package(PkgConfig QUIET)
if(TARGET libgit2::libgit2package)
  set(TOWEL_GIT2 libgit2::libgit2package)
elseif(PkgConfig_FOUND)
  pkg_check_modules(LIBGIT2 REQUIRED libgit2)
  add_library(towel_git2 INTERFACE)
  target_include_directories(towel_git2 INTERFACE ${LIBGIT2_INCLUDE_DIRS})
  if(TOWEL_STATIC_GIT2)
    target_link_options(towel_git2 INTERFACE ${LIBGIT2_STATIC_LDFLAGS_OTHER})
    target_link_directories(towel_git2 INTERFACE ${LIBGIT2_STATIC_LIBRARY_DIRS})
    target_link_libraries(towel_git2 INTERFACE ${LIBGIT2_STATIC_LIBRARIES})
  else()
    target_link_directories(towel_git2 INTERFACE ${LIBGIT2_LIBRARY_DIRS})
    target_link_libraries(towel_git2 INTERFACE ${LIBGIT2_LIBRARIES})
  endif()
  set(TOWEL_GIT2 towel_git2)
else()
  find_path(GIT2_INCLUDE git2.h REQUIRED)
  find_library(GIT2_LIB NAMES git2 libgit2 REQUIRED)
  add_library(towel_git2 INTERFACE)
  target_include_directories(towel_git2 INTERFACE ${GIT2_INCLUDE})
  target_link_libraries(towel_git2 INTERFACE ${GIT2_LIB})
  if(WIN32)
    target_link_libraries(towel_git2 INTERFACE ws2_32 rpcrt4 crypt32 ole32 secur32 winhttp advapi32)
  endif()
  set(TOWEL_GIT2 towel_git2)
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(towel WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/gitutil.h src/data.h src/theme.h src/theme.cpp
  src/gitload.h src/gitload.cpp
  src/repo.h src/repo.cpp
  src/commitmodel.h src/commitmodel.cpp
  src/commitview.h src/commitview.cpp
  src/diffdoc.h src/diffdoc.cpp
  src/diffview.h src/diffview.cpp
  src/panels.h src/panels.cpp
  src/commitpanel.h src/commitpanel.cpp src/rebaseview.h src/rebaseview.cpp
  src/avatars.h src/avatars.cpp
  src/mainwindow.h src/mainwindow.cpp
)
target_include_directories(towel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(towel PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Concurrent
  KF6::SyntaxHighlighting ${TOWEL_GIT2})

set_target_properties(towel PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Towel"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.pavel-teramips.towel"
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

install(TARGETS towel RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
  install(FILES towel.desktop DESTINATION share/applications)
  install(FILES icons/towel.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()
