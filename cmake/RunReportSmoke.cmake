foreach(_required CADVENTORY_EXECUTABLE SAMPLE_LIBRARY OUTPUT_PDF)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(REMOVE "${OUTPUT_PDF}")
execute_process(
  COMMAND "${CADVENTORY_EXECUTABLE}"
    --report "${SAMPLE_LIBRARY}"
    --output "${OUTPUT_PDF}"
    --depth 1
    --no-tags
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  TIMEOUT 280
)

if(NOT _result EQUAL 0)
  message(FATAL_ERROR
    "Report command failed (${_result})\nstdout:\n${_stdout}\nstderr:\n${_stderr}")
endif()
if(NOT EXISTS "${OUTPUT_PDF}")
  message(FATAL_ERROR "Report command did not create ${OUTPUT_PDF}")
endif()

file(SIZE "${OUTPUT_PDF}" _size)
if(_size LESS 5)
  message(FATAL_ERROR "Report PDF is empty")
endif()
file(READ "${OUTPUT_PDF}" _header LIMIT 4 HEX)
if(NOT _header STREQUAL "25504446")
  message(FATAL_ERROR "Report output is not a PDF")
endif()
