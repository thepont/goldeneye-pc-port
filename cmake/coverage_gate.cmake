# Deterministic unit-coverage gate for the pure PC-port policies.
#
# Formerly-red defect contracts are now ordinary green regression suites after
# their production fixes landed. The gate runs every green unit label and then
# measures the same run's gcov data.

if(NOT DEFINED GE007_COVERAGE_BUILD_DIR OR
   NOT DEFINED GE007_COVERAGE_CTEST_COMMAND OR
   NOT DEFINED GE007_COVERAGE_GCOV OR
   NOT DEFINED GE007_COVERAGE_REQUIRED_FILES)
  message(FATAL_ERROR "coverage_gate.cmake was not given its required paths")
endif()

file(REMOVE_RECURSE "${GE007_COVERAGE_BUILD_DIR}/coverage")
file(GLOB_RECURSE _GE007_OLD_GCDA
  "${GE007_COVERAGE_BUILD_DIR}/CMakeFiles/*_test.dir/*.gcda")
if(_GE007_OLD_GCDA)
  file(REMOVE ${_GE007_OLD_GCDA})
endif()

execute_process(
  COMMAND "${GE007_COVERAGE_CTEST_COMMAND}"
    --test-dir "${GE007_COVERAGE_BUILD_DIR}"
    --label-regex "^ge007-unit$"
    --output-on-failure
  RESULT_VARIABLE _GE007_GREEN_STATUS
  OUTPUT_VARIABLE _GE007_GREEN_OUTPUT
  ERROR_VARIABLE _GE007_GREEN_ERROR)
if(NOT _GE007_GREEN_STATUS EQUAL 0)
  message("${_GE007_GREEN_OUTPUT}")
  message("${_GE007_GREEN_ERROR}")
  message(FATAL_ERROR "green unit tests failed before coverage was measured")
endif()

file(MAKE_DIRECTORY "${GE007_COVERAGE_BUILD_DIR}/coverage")
file(GLOB_RECURSE _GE007_GCNO_FILES
  "${GE007_COVERAGE_BUILD_DIR}/CMakeFiles/*_test.dir/*.gcno")
if(NOT _GE007_GCNO_FILES)
  message(FATAL_ERROR "coverage gate found no gcno files")
endif()

set(_GE007_REPORT_FILES)
set(_GE007_REPORT_LINES)
set(_GE007_REPORT_BRANCHES)

foreach(_GE007_GCNO IN LISTS _GE007_GCNO_FILES)
  # Incremental builds can leave a .gcno behind after a source is removed from
  # a test target. Without a matching profile, gcov emits a misleading 0%
  # report that can shadow the live report for the same source file. Required
  # files are still checked below, so a genuinely unexecuted target fails
  # rather than being silently accepted.
  string(REGEX REPLACE "\\.gcno$" ".gcda" _GE007_GCDA "${_GE007_GCNO}")
  if(NOT EXISTS "${_GE007_GCDA}")
    continue()
  endif()
  get_filename_component(_GE007_OBJECT_DIR "${_GE007_GCNO}" DIRECTORY)
  execute_process(
    COMMAND "${GE007_COVERAGE_GCOV}" -b -c -o "${_GE007_OBJECT_DIR}"
            "${_GE007_GCNO}"
    WORKING_DIRECTORY "${GE007_COVERAGE_BUILD_DIR}/coverage"
    RESULT_VARIABLE _GE007_GCOV_STATUS
    OUTPUT_VARIABLE _GE007_GCOV_OUTPUT
    ERROR_VARIABLE _GE007_GCOV_ERROR)
  if(NOT _GE007_GCOV_STATUS EQUAL 0)
    message("${_GE007_GCOV_OUTPUT}")
    message("${_GE007_GCOV_ERROR}")
    message(FATAL_ERROR "gcov failed for ${_GE007_GCNO}")
  endif()

  set(_GE007_CURRENT_FILE "")
  set(_GE007_CURRENT_LINES "")
  set(_GE007_CURRENT_BRANCHES "")
  string(REPLACE "\n" ";" _GE007_GCOV_LINES "${_GE007_GCOV_OUTPUT}")
  foreach(_GE007_LINE IN LISTS _GE007_GCOV_LINES)
    if(_GE007_LINE MATCHES "^File '([^']+)'$")
      if(_GE007_CURRENT_FILE AND _GE007_CURRENT_LINES)
        list(APPEND _GE007_REPORT_FILES "${_GE007_CURRENT_FILE}")
        list(APPEND _GE007_REPORT_LINES "${_GE007_CURRENT_LINES}")
        if(_GE007_CURRENT_BRANCHES)
          list(APPEND _GE007_REPORT_BRANCHES "${_GE007_CURRENT_BRANCHES}")
        else()
          list(APPEND _GE007_REPORT_BRANCHES "100.00")
        endif()
      endif()
      set(_GE007_CURRENT_FILE "${CMAKE_MATCH_1}")
      set(_GE007_CURRENT_LINES "")
      set(_GE007_CURRENT_BRANCHES "")
    elseif(_GE007_LINE MATCHES
           "^Lines executed:([0-9]+(\\.[0-9]+)?)% of")
      if(_GE007_CURRENT_FILE AND NOT _GE007_CURRENT_LINES)
        set(_GE007_CURRENT_LINES "${CMAKE_MATCH_1}")
      endif()
    elseif(_GE007_LINE MATCHES
           "^Branches executed:([0-9]+(\\.[0-9]+)?)% of")
      if(_GE007_CURRENT_FILE AND NOT _GE007_CURRENT_BRANCHES)
        set(_GE007_CURRENT_BRANCHES "${CMAKE_MATCH_1}")
      endif()
    elseif(_GE007_LINE MATCHES "^No branches")
      set(_GE007_CURRENT_BRANCHES "100.00")
    endif()
  endforeach()
  if(_GE007_CURRENT_FILE AND _GE007_CURRENT_LINES)
    list(APPEND _GE007_REPORT_FILES "${_GE007_CURRENT_FILE}")
    list(APPEND _GE007_REPORT_LINES "${_GE007_CURRENT_LINES}")
    if(_GE007_CURRENT_BRANCHES)
      list(APPEND _GE007_REPORT_BRANCHES "${_GE007_CURRENT_BRANCHES}")
    else()
      list(APPEND _GE007_REPORT_BRANCHES "100.00")
    endif()
  endif()
endforeach()

# Header-only policies can be instrumented once for their dedicated contract
# test and again when an unrelated production TU includes them. gcov emits one
# report per object, so the same source path may occur more than once. Select
# the strongest observed report for a required file; otherwise an incidental
# partial copy can hide the complete direct-contract report.
function(ge007_best_coverage_for_file FILE_PATH OUT_LINES OUT_BRANCHES OUT_FOUND)
  set(_best_lines -1)
  set(_best_branches -1)
  set(_found FALSE)
  list(LENGTH _GE007_REPORT_FILES _report_count)
  if(_report_count GREATER 0)
    math(EXPR _last_report "${_report_count} - 1")
    foreach(_report_index RANGE 0 ${_last_report})
      list(GET _GE007_REPORT_FILES ${_report_index} _report_file)
      if("${_report_file}" STREQUAL "${FILE_PATH}")
        list(GET _GE007_REPORT_LINES ${_report_index} _report_lines)
        list(GET _GE007_REPORT_BRANCHES ${_report_index} _report_branches)
        set(_found TRUE)
        if(_report_lines GREATER _best_lines)
          set(_best_lines "${_report_lines}")
        endif()
        if(_report_branches GREATER _best_branches)
          set(_best_branches "${_report_branches}")
        endif()
      endif()
    endforeach()
  endif()
  set(${OUT_LINES} "${_best_lines}" PARENT_SCOPE)
  set(${OUT_BRANCHES} "${_best_branches}" PARENT_SCOPE)
  set(${OUT_FOUND} "${_found}" PARENT_SCOPE)
endfunction()

string(REPLACE "|" ";" _GE007_REQUIRED_FILES
  "${GE007_COVERAGE_REQUIRED_FILES}")
foreach(_GE007_REQUIRED IN LISTS _GE007_REQUIRED_FILES)
  get_filename_component(_GE007_REQUIRED_ABS "${_GE007_REQUIRED}" ABSOLUTE)
  ge007_best_coverage_for_file(
    "${_GE007_REQUIRED_ABS}"
    _GE007_LINE_PERCENT
    _GE007_BRANCH_PERCENT
    _GE007_REPORT_FOUND)
  if(NOT _GE007_REPORT_FOUND)
    message(FATAL_ERROR "coverage gate found no gcov report for ${_GE007_REQUIRED}")
  endif()

  if(_GE007_LINE_PERCENT LESS GE007_COVERAGE_MIN_LINES)
    message(FATAL_ERROR
      "${_GE007_REQUIRED}: line coverage ${_GE007_LINE_PERCENT}% is below ${GE007_COVERAGE_MIN_LINES}%")
  endif()
  if(_GE007_BRANCH_PERCENT LESS GE007_COVERAGE_MIN_BRANCHES)
    message(FATAL_ERROR
      "${_GE007_REQUIRED}: branch coverage ${_GE007_BRANCH_PERCENT}% is below ${GE007_COVERAGE_MIN_BRANCHES}%")
  endif()
  message(STATUS
    "coverage ${_GE007_REQUIRED}: lines=${_GE007_LINE_PERCENT}% branches=${_GE007_BRANCH_PERCENT}%")
endforeach()

# Production game math is a larger, legacy C slice than the small policy
# headers above. Keep it on the same deterministic gate with an explicit 80%
# floor instead of silently reporting only header coverage.
if(DEFINED GE007_COVERAGE_ADDITIONAL_FILES AND
   NOT "${GE007_COVERAGE_ADDITIONAL_FILES}" STREQUAL "")
  string(REPLACE "|" ";" _GE007_ADDITIONAL_FILES
    "${GE007_COVERAGE_ADDITIONAL_FILES}")
  foreach(_GE007_REQUIRED IN LISTS _GE007_ADDITIONAL_FILES)
    get_filename_component(_GE007_REQUIRED_ABS "${_GE007_REQUIRED}" ABSOLUTE)
    ge007_best_coverage_for_file(
      "${_GE007_REQUIRED_ABS}"
      _GE007_LINE_PERCENT
      _GE007_BRANCH_PERCENT
      _GE007_REPORT_FOUND)
    if(NOT _GE007_REPORT_FOUND)
      message(FATAL_ERROR "coverage gate found no gcov report for ${_GE007_REQUIRED}")
    endif()

    if(_GE007_LINE_PERCENT LESS GE007_COVERAGE_ADDITIONAL_MIN_LINES)
      message(FATAL_ERROR
        "${_GE007_REQUIRED}: line coverage ${_GE007_LINE_PERCENT}% is below ${GE007_COVERAGE_ADDITIONAL_MIN_LINES}%")
    endif()
    if(_GE007_BRANCH_PERCENT LESS GE007_COVERAGE_ADDITIONAL_MIN_BRANCHES)
      message(FATAL_ERROR
        "${_GE007_REQUIRED}: branch coverage ${_GE007_BRANCH_PERCENT}% is below ${GE007_COVERAGE_ADDITIONAL_MIN_BRANCHES}%")
    endif()
    message(STATUS
      "coverage ${_GE007_REQUIRED}: lines=${_GE007_LINE_PERCENT}% branches=${_GE007_BRANCH_PERCENT}% (additional floor)")
  endforeach()
endif()

# Keep the denominator visible.  The additional-file list is intentionally a
# deterministic seam, not a claim that every game translation unit executed.
# Report how much of src/game is represented so a passing per-file gate cannot
# be mistaken for whole-game coverage.
if(DEFINED GE007_COVERAGE_GAME_SOURCE_DIR AND
   NOT "${GE007_COVERAGE_GAME_SOURCE_DIR}" STREQUAL "")
  file(GLOB _GE007_ALL_GAME_SOURCES
    "${GE007_COVERAGE_GAME_SOURCE_DIR}/*.c")
  list(LENGTH _GE007_ALL_GAME_SOURCES _GE007_TOTAL_GAME_SOURCES)
  set(_GE007_SELECTED_GAME_SOURCES)
  foreach(_GE007_REQUIRED IN LISTS _GE007_ADDITIONAL_FILES)
    get_filename_component(_GE007_REQUIRED_ABS "${_GE007_REQUIRED}" ABSOLUTE)
    string(FIND "${_GE007_REQUIRED_ABS}"
      "${GE007_COVERAGE_GAME_SOURCE_DIR}/" _GE007_GAME_SOURCE_PREFIX)
    if(NOT _GE007_GAME_SOURCE_PREFIX EQUAL -1)
      list(APPEND _GE007_SELECTED_GAME_SOURCES "${_GE007_REQUIRED_ABS}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _GE007_SELECTED_GAME_SOURCES)
  list(LENGTH _GE007_SELECTED_GAME_SOURCES _GE007_SELECTED_GAME_SOURCES_COUNT)
  if(_GE007_TOTAL_GAME_SOURCES GREATER 0)
    math(EXPR _GE007_GAME_SOURCE_PERCENT
      "(${_GE007_SELECTED_GAME_SOURCES_COUNT} * 100) / ${_GE007_TOTAL_GAME_SOURCES}")
  else()
    set(_GE007_GAME_SOURCE_PERCENT 0)
  endif()
  message(STATUS
    "deterministic src/game translation-unit inventory: "
    "${_GE007_SELECTED_GAME_SOURCES_COUNT}/${_GE007_TOTAL_GAME_SOURCES} "
    "(${_GE007_GAME_SOURCE_PERCENT}%) selected")
endif()

message(STATUS "ge007 unit coverage gate passed")
