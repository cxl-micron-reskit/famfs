
cmake_minimum_required(VERSION 3.15)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-modules)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(CMAKE_COMPILER_IS_GNUCXX)
    message("Compiling for code coverage tests")
    include(CodeCoverage)
    append_coverage_compiler_flags()
  endif()
endif()

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_SOURCE_DIR}/usr")
include_directories("${PROJECT_SOURCE_DIR}/../kmod")
include_directories("${PROJECT_SOURCE_DIR}/testlib")

#file(GLOB sources "${PROJECT_SOURCE_DIR}/src/test/*.c")
#list(REMOVE_ITEM sources "${PROJECT_SOURCE_DIR}/src/main.c")

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
  target_link_libraries("${name}_tests" gtest_main libfamfs famfstest uuid )
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
	DEPENDENCIES gtest_main libfamfs
	#BASE_DIRECORY "../"
      )
    endif()
  endif()
endforeach()
