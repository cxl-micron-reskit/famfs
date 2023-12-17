
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb -Wall ")

project(famfs_unit)

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

add_executable(famfs famfs_cli.c )
add_executable(mkfs.famfs mkfs.famfs.c )

target_link_libraries(famfs libfamfs famfstest uuid z)
target_link_libraries(mkfs.famfs libfamfs uuid z)
target_link_libraries(libfamfs  uuid z)


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
  CONFIGURE_COMMAND   echo "Empty configure step for jtrace ${CMAKE_CURRENT_BINARY_DIR}"
  BUILD_COMMAND make clean all
  INSTALL_COMMAND ""
)

message("multichase: ${CMAKE_CURRENT_BINARY_DIR}")

#
# Installation
#
install(TARGETS famfs DESTINATION /usr/local/bin)
install(TARGETS mkfs.famfs DESTINATION /usr/local/bin)

#Install library and header files? Maybe later...

#install(TARGETS libfamfs DESTINATION /usr/local/lib)
#install(FILES famfs_llib.h DESTINATION /usr/local/include)

#Install man pages? Heck yeah, as soon as we have some...

# You can also install documentation or man pages
#install(FILES README.md DESTINATION /usr/local/share/doc/my_project)
#install(FILES famfs.1 DESTINATION /usr/local/share/man/man1)
