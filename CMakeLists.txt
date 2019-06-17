cmake_minimum_required(VERSION 3.1.0)
if(WIN32)
project(TimeWarp LANGUAGES CXX CSharp)
else(WIN32)
project(TimeWarp)
endif(WIN32)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#-----------------------------------------------------------------------------
# Local CMake Modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#-----------------------------------------------------------------------------
# Search under the install directory and in /usr/lib for packages.
list(APPEND CMAKE_PREFIX_PATH
    ${CMAKE_INSTALL_PREFIX}/lib/cmake
    usr/lib/cmake
)

#-----------------------------------------------------------------------------
# Enable sorting projects within the solution on Visual Studio, to group
# Test and Example files together.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#-----------------------------------------------------------------------------
# Checking for optional and required packages
find_package(AquetiTools CONFIG REQUIRED)

#-----------------------------------------------------------------------------
# Build options.
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TESTS "Build test programs" ON)

#-----------------------------------------------------------------------------
# Set things up for optional parameters
if (BUILD_SHARED_LIBS)
  option(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS "Export all symbols in Windows" ON)
endif()

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
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
)

set (TimeWarpLib_HEADERS
  TimeWarp.hpp
)

add_library(TimeWarp ${TimeWarpLib_SOURCES} ${TimeWarpLib_HEADERS})
set_target_properties(TimeWarp PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(TimeWarp PUBLIC
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

  if(WIN32)       # MS-Windows, both 32 and 64 bits
    add_executable(TimeWarp_client_example_CS examples/TimeWarp_client_example_CS.cs)
    set_target_properties(TimeWarp_client_example_CS PROPERTIES FOLDER examples)
	target_compile_options(TimeWarp_client_example_CS PUBLIC "/unsafe")
	install(TARGETS TimeWarp_client_example_CS EXPORT ${PROJECT_NAME}
      RUNTIME DESTINATION bin
    )
  endif(WIN32)

endif(BUILD_EXAMPLES)

#-----------------------------------------------------------------------------
# Build tests if we've been asked to.

if(BUILD_TESTS)

  enable_testing()

  # @todo Finish C and Python test suites to include C++ ones
  set (CPP_TESTS
    test_TimeWarp
  )
  foreach (APP ${CPP_TESTS})
    add_executable (${APP} tests/${APP}.cpp)
    set_target_properties(${APP} PROPERTIES FOLDER tests)
    target_link_libraries(${APP}
      TimeWarp
    )
    install(TARGETS ${APP} EXPORT ${PROJECT_NAME}
      RUNTIME DESTINATION bin
    )
    add_test(${APP} ${APP})
  endforeach ()

endif(BUILD_TESTS)

#############################################
#install library files
# This sections initiates the build of the  components in th TARGET_LIST. 
# Each component
#############################################
configure_file(${CMAKE_SOURCE_DIR}/${PROJECT_NAME}Config.cmake.in
               ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
               @ONLY)

export(TARGETS ${TARGET_LIST} FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake)

install(TARGETS ${TARGET_LIST} EXPORT ${PROJECT_NAME}
   RUNTIME DESTINATION bin COMPONENT lib
   LIBRARY DESTINATION lib${LIB_SUFFIX} COMPONENT lib
   ARCHIVE DESTINATION lib${LIB_SUFFIX} COMPONENT lib
   INCLUDES DESTINATION include/atl
   PUBLIC_HEADER DESTINATION include/atl
)

install( FILES ${TimeWarpLib_HEADERS} DESTINATION include/atl)

install(FILES ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        DESTINATION lib${LIB_SUFFIX}/cmake)
install(EXPORT ${PROJECT_NAME}
        DESTINATION lib${LIB_SUFFIX}/cmake
        FILE ${PROJECT_NAME}Targets.cmake)

