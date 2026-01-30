
cmake_minimum_required(VERSION 3.10...3.28)

project(famfs C CXX )

# Uncomment for clang - which detects different issues than gcc...
# set(CMAKE_C_COMPILER clang)

# Require a modern C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ...and a modern C++ standard for unit tests / googletest
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global warning flags
add_compile_options(-Wall -Wextra)

# Debug flags
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb")

#
# Locate famfs ioctl headers
#
find_path(LINUX_INCLUDE_BASE
    NAMES linux/famfs_ioctl.h
    PATHS /usr/include "${PROJECT_SOURCE_DIR}/linux_include"
    NO_DEFAULT_PATH
)

if (NOT LINUX_INCLUDE_BASE)
    message(FATAL_ERROR "Could not find linux/famfs_ioctl.h in either system or fallback path (${PROJECT_SOURCE_DIR}/linux_include)")
endif()

add_compile_definitions(_DEFAULT_SOURCE _XOPEN_SOURCE=700)

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
      "--gcov-ignore-parse-errors=all" "--merge-mode-functions=separate"
    )
    append_coverage_compiler_flags()
    add_compile_definitions(FAMFS_COVERAGE)
  endif()
endif()

include(CheckIncludeFile)
check_include_file("linux/famfs_ioctl.h" HAVE_LINUX_FAMFS_IOCTL_H)
if (NOT HAVE_LINUX_FAMFS_IOCTL_H)
    message(STATUS "Using bundled famfs_ioctl.h from linux_include/")
    set(LINUX_INCLUDE_BASE "${CMAKE_SOURCE_DIR}/linux_include")
endif()

add_library(libfamfs
    src/famfs_lib.c
    src/famfs_alloc.c
    src/famfs_misc.c
    src/famfs_yaml.c
    src/famfs_fmap.c
    src/famfs_mount.c
    src/famfs_debug.c
    src/famfs_log.c
    src/famfs_dax.c
)

target_include_directories(libfamfs
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/testlib
        ${PROJECT_SOURCE_DIR}/libfuse/include
        ${PROJECT_SOURCE_DIR}/libfuse/lib
        ${CMAKE_CURRENT_BINARY_DIR}/libfuse
        ${LINUX_INCLUDE_BASE}
)

#
# Thread pool library (from C-Thread-Pool)
#
add_library(cthreadpool STATIC
    C-Thread-Pool/thpool.c
)
target_include_directories(cthreadpool PUBLIC
    ${PROJECT_SOURCE_DIR}/C-Thread-Pool
)
target_link_libraries(cthreadpool PUBLIC pthread)

#
# libfamfs now depends on the thread pool library
#
target_link_libraries(libfamfs PUBLIC cthreadpool)

add_library(libpcq src/pcq_lib.c)

target_include_directories(libpcq
    PUBLIC
        ${PROJECT_SOURCE_DIR}/C-Thread-Pool
        ${PROJECT_SOURCE_DIR}/testlib
        ${LINUX_INCLUDE_BASE}
)
target_include_directories(libpcq PUBLIC
    ${PROJECT_SOURCE_DIR}/C-Thread-Pool
)

target_link_libraries(libpcq PUBLIC cthreadpool)

add_library(libicache_obj OBJECT src/famfs_fused_icache.c)

target_include_directories(libicache_obj PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/C-Thread-Pool
        ${PROJECT_SOURCE_DIR}/mongoose
        ${PROJECT_SOURCE_DIR}/libfuse/include
        ${PROJECT_SOURCE_DIR}/libfuse/lib
        ${CMAKE_CURRENT_BINARY_DIR}/libfuse
        ${LINUX_INCLUDE_BASE}
)

add_library(libicache STATIC $<TARGET_OBJECTS:libicache_obj>)

add_library(libmongoose mongoose/mongoose.c)



target_include_directories(libmongoose
    PUBLIC
        ${PROJECT_SOURCE_DIR}/mongoose
        ${LINUX_INCLUDE_BASE}
)

target_compile_definitions(libmongoose
    PUBLIC MG_ENABLE_UNIX=1
)

add_executable(famfs src/famfs_cli.c)
add_executable(mkfs.famfs src/mkfs.famfs.c)
add_executable(pcq src/pcq.c)
add_executable(famfs_fused src/famfs_fused.c src/famfs_fused_rest.c)

find_package(PkgConfig REQUIRED)
if (NOT PKG_CONFIG_FOUND)
    message(FATAL_ERROR "pkg-config not found. Install it (Fedora: pkgconf-pkg-config; Debian/Ubuntu: pkg-config).")
endif()

pkg_check_modules(DAXCTL REQUIRED libdaxctl)
pkg_check_modules(CURL REQUIRED libcurl)

target_link_libraries(libfamfs PRIVATE uuid z yaml ${DAXCTL_LIBRARIES} ${CURL_LIBRARIES})
target_include_directories(libfamfs PRIVATE ${CURL_INCLUDE_DIRS})
target_include_directories(libfamfs PRIVATE ${DAXCTL_INCLUDE_DIRS})

target_link_libraries(famfs libfamfs famfstest uuid z yaml)
target_link_libraries(mkfs.famfs libfamfs uuid z yaml)
target_link_libraries(pcq libpcq libfamfs uuid z famfstest yaml)

target_link_libraries(famfs_fused
    PRIVATE
        libfamfs
        libmongoose
        "${CMAKE_CURRENT_BINARY_DIR}/libfuse/lib/libfuse3.so"
        pthread dl rt systemd
)

# Explicitly add the object file from libicache_obj
target_sources(famfs_fused PRIVATE $<TARGET_OBJECTS:libicache_obj>)


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
