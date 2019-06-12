cmake_minimum_required(VERSION 3.1.0)
project(TimeWarp)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#-----------------------------------------------------------------------------
# Local CMake Modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#-----------------------------------------------------------------------------
# Enable sorting projects within the solution on Visual Studio, to group
# Test and Example files together.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#-----------------------------------------------------------------------------
# Checking for optional and required packages
find_package(AquetiTools CONFIG REQUIRED)

#-----------------------------------------------------------------------------
# Build options.
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TESTS "Build test programs" ON)

#-----------------------------------------------------------------------------
# Set things up for optional parameters

#-----------------------------------------------------------------------------
# Set up build product locations

include(GNUInstallDirs)

# Win-specific: we want shared libs (dlls) in same dir as exe files.
if(WIN32)
	set(DISPLAYSERVERCLIENT_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_BINDIR}")
else()
	set(DISPLAYSERVERCLIENT_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()

set (TimeWarpLib_SOURCES TimeWarp.cpp)

#-----------------------------------------------------------------------------
# Build the library.

# Hide all symbols that are not explicitly exported from the library
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set (TimeWarpLib_HEADERS
  TimeWarp.hpp
)

add_library(TimeWarp STATIC ${TimeWarpLib_SOURCES} ${TimeWarpLib_HEADERS})
set_target_properties(TimeWarp PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(TimeWarp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_INSTALL_PREFIX}/include
)
target_link_libraries(TimeWarp PUBLIC AquetiTools)
if(UNIX)
  target_link_libraries(TimeWarp PUBLIC pthread)
endif(UNIX)
if(WIN32)       # MS-Windows, both 32 and 64 bits
  target_link_libraries(TimeWarp PUBLIC wsock32)
endif(WIN32)
set_target_properties(TimeWarp PROPERTIES PUBLIC_HEADER "${AQT_HEADERS}")

install(TARGETS TimeWarp EXPORT ${PROJECT_NAME}
  RUNTIME DESTINATION bin COMPONENT lib
  LIBRARY DESTINATION lib${LIB_SUFFIX} COMPONENT lib
  ARCHIVE DESTINATION lib${LIB_SUFFIX} COMPONENT lib
  INCLUDES DESTINATION include
  PUBLIC_HEADER DESTINATION include
)

# Back to normal symbol visibility
set(CMAKE_CXX_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 0)

#-----------------------------------------------------------------------------
# Build examples if we've been asked to.

if(BUILD_EXAMPLES)
  set (EXAMPLES
    TimeWarp_server_example
    TimeWarp_client_example
  )
  foreach (BASE ${EXAMPLES})
    # C++ example
    set (APP ${BASE})
    add_executable (${APP} examples/${BASE}.cpp)
    set_target_properties(${APP} PROPERTIES FOLDER examples)
    target_link_libraries(${APP} TimeWarp)
    install(TARGETS ${APP} EXPORT ${PROJECT_NAME}
      RUNTIME DESTINATION bin
    )
  endforeach (BASE)

endif(BUILD_EXAMPLES)

#-----------------------------------------------------------------------------
# Build tests if we've been asked to.

if(BUILD_TESTS)

  enable_testing()

  # @todo Finish C and Python test suites to include C++ ones
  set (CPP_TESTS
    test_TimeWarp
  )
  foreach (BASE ${CPP_TESTS})
    set (APP ${BASE}_cpp)
    add_executable (${APP} tests/${BASE}.cpp)
    set_target_properties(${APP} PROPERTIES FOLDER tests)
    target_link_libraries(${APP}
      TimeWarp
    )
    install(TARGETS ${APP} EXPORT ${PROJECT_NAME}
      RUNTIME DESTINATION bin
    )
    add_test(${APP} ${APP})
  endforeach (BASE)

endif(BUILD_TESTS)

