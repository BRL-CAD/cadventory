find_package(Qt6 REQUIRED COMPONENTS Test)

include(FetchContent)
find_package(Catch2 3 QUIET)
if(NOT TARGET Catch2::Catch2WithMain)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.1
  )
  FetchContent_MakeAvailable(Catch2)
endif()

function(add_cadventory_test)
  set(options GUI SMOKE PERFORMANCE FAILURE)
  set(oneValueArgs NAME)
  cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT TEST_NAME)
    message(FATAL_ERROR "add_cadventory_test requires NAME")
  endif()

  qt_add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
  target_link_libraries(${TEST_NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Test
  )

  if(TEST_GUI)
    target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/assets/images.qrc)
    target_link_libraries(${TEST_NAME} PRIVATE cadventory_application)
    set(_labels release gui)
  else()
    target_link_libraries(${TEST_NAME} PRIVATE cadventory_domain)
    set(_labels release headless)
  endif()

  if(TEST_SMOKE)
    list(APPEND _labels smoke)
  endif()
  if(TEST_PERFORMANCE)
    list(APPEND _labels performance)
  endif()
  if(TEST_FAILURE)
    list(APPEND _labels failure)
  endif()

  target_compile_definitions(${TEST_NAME} PRIVATE
    BRLCAD_BUILD="${BRLCAD_ROOT}"
    TEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )

  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  set_tests_properties(${TEST_NAME} PROPERTIES LABELS "${_labels}")
  if(TEST_GUI)
    set_tests_properties(${TEST_NAME} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endfunction()

add_cadventory_test(NAME ModelTest)
add_cadventory_test(NAME LibraryTest)
add_cadventory_test(NAME FilesystemIndexerTest)
add_cadventory_test(NAME FilesystemIndexerPerfTest PERFORMANCE)
add_cadventory_test(NAME SimpleFileLockTest)
add_cadventory_test(NAME SimpleFileLockPerfTest PERFORMANCE)

if(CADVENTORY_WITH_GUI)
  add_cadventory_test(NAME GeometryBrowserDialogTest GUI)
  add_cadventory_test(NAME FileSystemModelWithCheckboxesTest GUI)
  add_cadventory_test(NAME FileSystemFilterProxyModelTest GUI)
  add_cadventory_test(NAME ModelCardDelegateTest GUI)
  add_cadventory_test(NAME MainWindowTest GUI)
  add_cadventory_test(NAME LibraryWindowTest GUI)
endif()

add_cadventory_test(NAME ProcessGFilesTest SMOKE)
add_cadventory_test(NAME IndexingWorkerTest)
add_cadventory_test(NAME JobServiceTest)
add_cadventory_test(NAME JobWorkerTest)
add_cadventory_test(NAME JobManagerTest)
add_cadventory_test(NAME SQLJobQueueTest)
add_cadventory_test(NAME SQLiteDBTest)
add_cadventory_test(NAME SQLModelRepositoryTest)
add_cadventory_test(NAME ModelsManagerTest)
add_cadventory_test(NAME QtModelsListModelTest)
add_cadventory_test(NAME OllamaCliServiceTest FAILURE)

qt_add_executable(ProcessRunnerTestHelper ProcessRunnerTestHelper.cpp)
target_link_libraries(ProcessRunnerTestHelper PRIVATE Qt6::Core)

add_cadventory_test(NAME ProcessRunnerTest FAILURE)
target_compile_definitions(ProcessRunnerTest PRIVATE
  PROCESS_RUNNER_TEST_HELPER="$<TARGET_FILE:ProcessRunnerTestHelper>")
add_dependencies(ProcessRunnerTest ProcessRunnerTestHelper)

set(_report_smoke_pdf "${CMAKE_CURRENT_BINARY_DIR}/report-smoke.pdf")
add_test(
  NAME ReportGenerationSmokeTest
  COMMAND ${CMAKE_COMMAND}
    -DCADVENTORY_EXECUTABLE=$<TARGET_FILE:cadventory>
    -DSAMPLE_LIBRARY=${CMAKE_CURRENT_SOURCE_DIR}
    -DOUTPUT_PDF=${_report_smoke_pdf}
    -P ${CMAKE_SOURCE_DIR}/cmake/RunReportSmoke.cmake
)
set_tests_properties(ReportGenerationSmokeTest PROPERTIES
  LABELS "release;headless;smoke"
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 300
)

add_test(
  NAME ReportGenerationFailureTest
  COMMAND ${CMAKE_COMMAND}
    -DCADVENTORY_EXECUTABLE=$<TARGET_FILE:cadventory>
    -DMISSING_LIBRARY=${CMAKE_CURRENT_BINARY_DIR}/missing-report-library
    -DOUTPUT_PDF=${CMAKE_CURRENT_BINARY_DIR}/failed-report.pdf
    -P ${CMAKE_SOURCE_DIR}/cmake/RunReportFailure.cmake
)
set_tests_properties(ReportGenerationFailureTest PROPERTIES
  LABELS "release;headless;failure"
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 30
)
