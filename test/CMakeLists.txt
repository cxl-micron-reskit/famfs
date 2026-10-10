
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
target_link_libraries("${name}_tests" gtest_main libfamfs famfstest uuid famfs_unit_testlib)

target_sources("${name}_tests" PRIVATE $<TARGET_OBJECTS:libicache_obj>)

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

#
# pmd_fault_check: standalone smoke-test helper (no gtest, no libfamfs) that
# verifies famfs mmaps are populated by PMD faults. It lands in the build
# root next to famfs so the smoke scripts find it via $BIN.
#
add_executable(pmd_fault_check pmd_fault_check.c)
set_target_properties(pmd_fault_check PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
#
# The helper counts its own page faults, so it must not be instrumented: an
# ASan build (make sanitize) takes one extra fault per 4 KiB of shadow memory
# (2048 extra for a 64 MiB mapping), which reads as a PTE/PMD mix and fails
# the test even though the kernel trace shows only PMD faults. These options
# come after CMAKE_C_FLAGS on the command line, so they win.
#
target_compile_options(pmd_fault_check PRIVATE -fno-sanitize=all)
target_link_options(pmd_fault_check PRIVATE -fno-sanitize=all)
