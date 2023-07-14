
cmake_minimum_required(VERSION 3.1)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb")

project(tagfs_test)

##
### Source definitions ###
##

message(STATUS "PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}../tagfs")

add_library(libtagfs tagfs_lib.c random_buffer.c xrand.c )

add_executable(mkfile mmap_test.c )
add_executable(mmap_test mmap_test.c )
add_executable(tagfs tagfs_cli.c )
add_executable(mkfs.tagfs mkfs.tagfs.c )

target_link_libraries(tagfs libtagfs uuid)
target_link_libraries(mkfs.tagfs libtagfs uuid)
target_link_libraries(mkfile libtagfs uuid)



##
### Test definitions ###
##

#configure_file(CMakeLists.txt.in
#        googletest-download/CMakeLists.txt)
#execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
#        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
#execute_process(COMMAND ${CMAKE_COMMAND} --build .
#        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
#
#add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
#        ${CMAKE_BINARY_DIR}/googletest-build)
#
#enable_testing()


