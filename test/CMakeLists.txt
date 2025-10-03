
cmake_minimum_required(VERSION 3.10...3.28)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-modules)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(CMAKE_COMPILER_IS_GNUCXX)
    message("Compiling for code coverage tests")
    include(CodeCoverage)
    append_coverage_compiler_flags()
  endif()
endif()

add_compile_options(-Wno-write-strings)

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_SOURCE_DIR}/usr")
include_directories("${PROJECT_SOURCE_DIR}/../kmod")
include_directories("${PROJECT_SOURCE_DIR}/testlib")

add_library(famfs_unit_testlib famfs_unit.c)
#
# Make sure famfs_unit_testlib can see famfs headers (e.g., src/famfs_lib.h)
# via libfamfs' PUBLIC include directories.
#
target_link_libraries(famfs_unit_testlib PRIVATE libfamfs)
target_include_directories(famfs_unit_testlib PRIVATE ${LINUX_INCLUDE_BASE})
 
file(GLOB tests "${PROJECT_SOURCE_DIR}/test/*.cpp")
#list(REMOVE_ITEM tests "${PROJECT_SOURCE_DIR}/test/main.cpp")

set(COVERAGE_EXCLUDES "./coverage" "./debug")
#  ${PROJECT_SOURCE_DIR}/debug/googletest-src/*" )
message(STATUS "tests=${tests}")
foreach(file ${tests})
  set(name)
  get_filename_component(name ${file} NAME_WE)
  add_executable("${name}_tests"
    ${sources}
    ${file}
    )
#    "${PROJECT_SOURCE_DIR}/test/main.cpp")
  target_link_libraries("${name}_tests" gtest_main libfamfs famfstest libicache uuid famfs_unit_testlib)

  message(STATUS "name=${name}")
  add_test(NAME ${name} COMMAND "${name}_tests")

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (CMAKE_COMPILER_IS_GNUCXX AND FAMFS_TEST_COVERAGE)
      #include(CodeCoverage)
      set(GCOVR_PATH "/usr/bin/gcovr")

      # Works
      setup_target_for_coverage_gcovr_html(
	NAME "${name}_coverage"
	EXECUTABLE "${name}_tests"
	DEPENDENCIES gtest_main libfamfs famfstest famfs_unit_testlib
	#BASE_DIRECORY "../"
      )
    endif()
  endif()
endforeach()
