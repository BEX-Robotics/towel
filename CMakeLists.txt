cmake_minimum_required(VERSION 3.21)
project(towel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
find_package(KF6SyntaxHighlighting REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(towel
  src/main.cpp
  src/gitutil.h src/data.h src/theme.h src/theme.cpp
  src/gitload.h src/gitload.cpp
  src/repo.h src/repo.cpp
  src/commitmodel.h src/commitmodel.cpp
  src/commitview.h src/commitview.cpp
  src/diffdoc.h src/diffdoc.cpp
  src/diffview.h src/diffview.cpp
  src/panels.h src/panels.cpp
  src/commitpanel.h src/commitpanel.cpp
  src/avatars.h src/avatars.cpp
  src/mainwindow.h src/mainwindow.cpp
)
target_include_directories(towel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(towel PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Concurrent
  KF6::SyntaxHighlighting PkgConfig::LIBGIT2)

install(TARGETS towel RUNTIME DESTINATION bin)
