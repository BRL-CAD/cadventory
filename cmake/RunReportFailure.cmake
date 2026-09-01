foreach(_required CADVENTORY_EXECUTABLE MISSING_LIBRARY OUTPUT_PDF)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${MISSING_LIBRARY}")
file(REMOVE "${OUTPUT_PDF}")
execute_process(
  COMMAND "${CADVENTORY_EXECUTABLE}"
    --report "${MISSING_LIBRARY}"
    --output "${OUTPUT_PDF}"
    --no-tags
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  TIMEOUT 20
)

if(_result EQUAL 0)
  message(FATAL_ERROR
    "Invalid report input unexpectedly succeeded\nstdout:\n${_stdout}\nstderr:\n${_stderr}")
endif()
if(EXISTS "${OUTPUT_PDF}")
  message(FATAL_ERROR "Failed report left an output artifact")
endif()
