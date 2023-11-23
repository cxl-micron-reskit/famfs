
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb -Wall ")

project(famfs_test)

include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/../kmod")
include_directories("${PROJECT_SOURCE_DIR}/testlib")


##
### Source definitions ###
##

message(STATUS "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}../famfs")

add_library(libfamfs famfs_lib.c  )

add_executable(mkfile mmap_test.c )
add_executable(mmap_test mmap_test.c )
add_executable(famfs famfs_cli.c )
add_executable(mkfs.famfs mkfs.famfs.c )

target_link_libraries(famfs libfamfs famfstest uuid)
target_link_libraries(mkfs.famfs libfamfs uuid)
target_link_libraries(mkfile libfamfs uuid)



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

