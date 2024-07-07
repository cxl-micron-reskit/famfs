
cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb -Wall ")

project(famfs)


include_directories("${PROJECT_SOURCE_DIR}/src")
include_directories("${PROJECT_SOURCE_DIR}/testlib")
include_directories("${PROJECT_SOURCE_DIR}/libfuse/include")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/libfuse")

# Don't build against instlaled libfuse...
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
    append_coverage_compiler_flags()
  endif()
endif()

add_library(libfamfs src/famfs_lib.c  )
add_library(libpcq src/pcq_lib.c  )

add_executable(famfs src/famfs_cli.c )
add_executable(mkfs.famfs src/mkfs.famfs.c )
add_executable(pcq src/pcq.c )

target_link_libraries(famfs libfamfs famfstest uuid z)
target_link_libraries(mkfs.famfs libfamfs uuid z)
target_link_libraries(libfamfs  uuid z)
target_link_libraries(pcq libpcq libfamfs uuid z famfstest)

add_executable(passthrough_ll fuse/passthrough_ll.c)
add_executable(passthrough_hp fuse/passthrough_hp.cc)

link_directories("${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib")
target_link_libraries(passthrough_ll "${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib/libfuse3.so"  pthread dl rt systemd )
target_link_libraries(passthrough_hp "${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib/libfuse3.so"  pthread dl rt)

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
