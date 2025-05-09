
cmake_minimum_required(VERSION 3.15)

# Uncomment for clang - which detects different issues than gcc...
#set(CMAKE_C_COMPILER clang)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb -Wall ")

project(famfs)


include_directories("${PROJECT_SOURCE_DIR}/src")
include_directories("${PROJECT_SOURCE_DIR}/C-Thread-Pool")
include_directories("${PROJECT_SOURCE_DIR}/testlib")
include_directories("${PROJECT_SOURCE_DIR}/libfuse/include")
include_directories("${PROJECT_SOURCE_DIR}/libfuse/lib")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/libfuse")

# Make the build work regardless of whether /usr/include/linux/famfs_ioctl.h
# exists. This is for the transition from standalone to fuse-based famfs
# Look for a directory that contains a 'linux/foo.h' path
find_path(LINUX_INCLUDE_BASE
    NAMES linux/famfs_ioctl.h
    PATHS /usr/include "${PROJECT_SOURCE_DIR}/linux_include"
    NO_DEFAULT_PATH
)

if (NOT LINUX_INCLUDE_BASE)
    message(FATAL_ERROR "Could not find linux/famfs_ioctl.h in either system or fallback path (${PROJECT_SOURCE_DIR}/linux_include)")
endif()

# Add base path — the one that contains the 'linux/' subdir
include_directories(${LINUX_INCLUDE_BASE})


# Don't build against installed libfuse...
# include_directories("/usr/local/include/fuse3")

##
### Source definitions ###
##

message(STATUS "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}")

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-modules)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(CMAKE_COMPILER_IS_GNUCXX AND FAMFS_TEST_COVERAGE)
    message("Compiling for code coverage tests")
    include(CodeCoverage)
    set(GCOVR_ADDITIONAL_ARGS
      "--gcov-ignore-parse-errors" "all"
    )
    append_coverage_compiler_flags()
  endif()
endif()

add_library(libfamfs src/famfs_lib.c src/famfs_alloc.c src/famfs_misc.c src/famfs_yaml.c src/famfs_fmap.c src/famfs_mount.c src/famfs_debug.c C-Thread-Pool/thpool.c )
add_library(libpcq src/pcq_lib.c  )

add_executable(famfs src/famfs_cli.c )
add_executable(mkfs.famfs src/mkfs.famfs.c )
add_executable(pcq src/pcq.c )
add_executable(famfs_fused src/famfs_fused.c)

target_link_libraries(famfs libfamfs famfstest uuid z yaml)
target_link_libraries(mkfs.famfs libfamfs uuid z yaml)
target_link_libraries(libfamfs  uuid z yaml)
target_link_libraries(pcq libpcq libfamfs uuid z famfstest yaml)
target_link_libraries(famfs_fused libfamfs)

link_directories("${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib")
target_link_libraries(famfs_fused "${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib/libfuse3.so"  pthread dl rt systemd )
#target_link_libraries(passthrough_hp "${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib/libfuse3.so"  pthread dl rt)

#
## Test definitions ###
#

configure_file(CMakeLists.txt.in
        googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
execute_process(COMMAND ${CMAKE_COMMAND} --build .
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )

add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
        ${CMAKE_BINARY_DIR}/googletest-build)

enable_testing()

add_subdirectory(testlib)
add_subdirectory(test)

include(ExternalProject)

ExternalProject_Add(multichase
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
  GIT_REPOSITORY "https://github.com/jagalactic/multichase.git"
  GIT_TAG dax
  UPDATE_COMMAND ""
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND   echo "Empty configure step for multichase ${CMAKE_CURRENT_BINARY_DIR}"
  BUILD_COMMAND make clean all
  INSTALL_COMMAND ""
)

message("multichase: ${CMAKE_CURRENT_BINARY_DIR}")

#
# Installation
#
install(TARGETS famfs DESTINATION /usr/local/bin)
install(TARGETS mkfs.famfs DESTINATION /usr/local/bin)
install(TARGETS pcq DESTINATION /usr/local/bin)

#Install library and header files? Maybe later...

#install(TARGETS libfamfs DESTINATION /usr/local/lib)
#install(FILES famfs_lib.h DESTINATION /usr/local/include)

#Install man pages? Heck yeah, as soon as we have some...

# You can also install documentation or man pages
#install(FILES README.md DESTINATION /usr/local/share/doc/my_project)
#install(FILES famfs.1 DESTINATION /usr/local/share/man/man1)
