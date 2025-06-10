cmake_minimum_required(VERSION 3.25)

project(CADventoryTests VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Enable coverage flags in Debug mode
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Adding code coverage flags")

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC-specific coverage flags
        add_compile_options(-fprofile-arcs -ftest-coverage -g -O0)
        link_libraries(gcov)  # gcov library for coverage
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang-specific coverage flags
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping -g -O0)
        add_link_options(-fprofile-instr-generate)
    else()
        message(WARNING "Code coverage is not supported for this compiler.")
    endif()
endif()

# Enable Qt's automatic processing
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


# Include directories from parent scope
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}      # Include the main build directory where config.h is located
    ${CMAKE_BINARY_DIR}/src
    ${BRLCAD_INCLUDE_DIRS}
)

# Find required Qt modules
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Test)
qt_standard_project_setup()

# Include FetchContent module for Catch2
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.1  # Specify the version you need
)
FetchContent_MakeAvailable(Catch2)

# Common libraries and include directories
set(COMMON_LIBRARIES
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    Qt6::Concurrent
    ${SQLITE_LIBRARY}
    ${BRLCAD_LIBRARIES}
)

set(COMMON_INCLUDE_DIRECTORIES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/src
    ${BRLCAD_INCLUDE_DIRS}
)

set(BRLCAD_BUILD "${BRLCAD_ROOT}")



# Function to add a test executable using qt_add_executable
# function(add_cadventory_test)
#     set(options)
#     set(oneValueArgs NAME)
#     set(multiValueArgs SOURCES)
#     cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

#     if(NOT TEST_NAME)
#         message(FATAL_ERROR "add_cadventory_test requires a NAME argument.")
#     endif()

#     qt_add_executable(${TEST_NAME}
#         ${TEST_SOURCES}
#     )

#     target_link_libraries(${TEST_NAME} PRIVATE ${COMMON_LIBRARIES})
#     target_include_directories(${TEST_NAME} PRIVATE ${COMMON_INCLUDE_DIRECTORIES})

#     add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
# endfunction()
# Function to add a test executable using qt_add_executable
function(add_cadventory_test)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TEST_NAME)
        message(FATAL_ERROR "add_cadventory_test requires a NAME argument.")
    endif()

    qt_add_executable(${TEST_NAME}
        ${TEST_SOURCES}
    )

    target_link_libraries(${TEST_NAME} PRIVATE ${COMMON_LIBRARIES})
    target_include_directories(${TEST_NAME} PRIVATE ${COMMON_INCLUDE_DIRECTORIES})

    # Define BRLCAD_BUILD for use in the source code
    target_compile_definitions(${TEST_NAME} PRIVATE BRLCAD_BUILD="${BRLCAD_ROOT}" RT_DLL_IMPORTS)

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()


# Add tests using the function
add_cadventory_test(
    NAME ModelTest
    SOURCES
        ModelTest.cpp
        ../Model.cpp
)

add_cadventory_test(
    NAME LibraryTest
    SOURCES
        LibraryTest.cpp
        ../Library.cpp
        ../Model.cpp
        ../FilesystemIndexer.cpp
)

add_cadventory_test(
    NAME FilesystemIndexerTest
    SOURCES
        FilesystemIndexerTest.cpp
        ../FilesystemIndexer.cpp
)

add_cadventory_test(
    NAME FilesystemIndexerPerfTest
    SOURCES
        FilesystemIndexerPerfTest.cpp
        ../FilesystemIndexer.cpp
)

add_cadventory_test(
    NAME GeometryBrowserDialogTest
    SOURCES
        GeometryBrowserDialogTest.cpp
        ../GeometryBrowserDialog.cpp
        ../Model.cpp
)

add_cadventory_test(
    NAME FileSystemModelWithCheckboxesTest
    SOURCES
        FileSystemModelWithCheckboxesTest.cpp
        ../FileSystemModelWithCheckboxes.cpp
        ../Model.cpp
)

add_cadventory_test(
    NAME FileSystemFilterProxyModelTest
    SOURCES
        FileSystemFilterProxyModelTest.cpp
        ../FileSystemFilterProxyModel.cpp
)

add_cadventory_test(
    NAME ProcessGFilesTest
    SOURCES
        ProcessGFilesTest.cpp
        ../ProcessGFiles.cpp
        ../Model.cpp
)

add_cadventory_test(
    NAME IndexingWorkerTest
    SOURCES
        IndexingWorkerTest.cpp
        ../IndexingWorker.cpp
        ../Library.cpp
        ../Model.cpp
        ../ProcessGFiles.cpp
        ../FilesystemIndexer.cpp
)

add_cadventory_test(
    NAME ModelCardDelegateTest
    SOURCES
        ModelCardDelegateTest.cpp
        ../ModelCardDelegate.cpp
        ../Model.cpp
)

# For tests requiring UI and resources
add_cadventory_test(
    NAME MainWindowTest
    SOURCES
        MainWindowTest.cpp
        ../MainWindow.cpp
        ../LibraryWindow.cpp
        ../Library.cpp
        ../Model.cpp
        ../ProcessGFiles.cpp
        ../IndexingWorker.cpp
        ../FilesystemIndexer.cpp
        ../ModelCardDelegate.cpp
        ../GeometryBrowserDialog.cpp
        ../ReportGenerationWindow.cpp
        ../ReportGeneratorWorker.cpp
        ../ModelFilterProxyModel.cpp
        ../ModelView.cpp
        ../ModelTagging.cpp
        ../ModelParser.cpp
        ../executeCommand.cpp
        ../FileSystemModelWithCheckboxes.cpp
        ../FileSystemFilterProxyModel.cpp
        ../SettingWindow.cpp
        # UI files
        ../mainwindow.ui
        ../librarywindow.ui
        ../reportgenerationwindow.ui
        ../SettingWindow.ui
        ../modelview.ui
        # Resource files
        ${CMAKE_SOURCE_DIR}/images.qrc
        # FIXME: for some reason AUTOMOC wont resolve CADventory::Q_OBJECT so explicitly pass in the header
        ../CADventory.h
)

add_cadventory_test(
    NAME LibraryWindowTest
    SOURCES
        LibraryWindowTest.cpp
        ../LibraryWindow.cpp
        ../MainWindow.cpp               # Include MainWindow.cpp
        ../Library.cpp
        ../Model.cpp
        ../ProcessGFiles.cpp
        ../IndexingWorker.cpp
        ../FilesystemIndexer.cpp
        ../ModelCardDelegate.cpp
        ../GeometryBrowserDialog.cpp
        ../ReportGenerationWindow.cpp
        ../ReportGeneratorWorker.cpp
        ../ModelFilterProxyModel.cpp
        ../ModelView.cpp
        ../ModelTagging.cpp
        ../ModelParser.cpp
        ../executeCommand.cpp
        ../FileSystemModelWithCheckboxes.cpp
        ../FileSystemFilterProxyModel.cpp
        ../SettingWindow.cpp
        # UI files
        ../mainwindow.ui
        ../librarywindow.ui
        ../reportgenerationwindow.ui
        ../SettingWindow.ui
        ../modelview.ui
        # Resource files
        ${CMAKE_SOURCE_DIR}/images.qrc
        # FIXME: for some reason AUTOMOC wont resolve CADventory::Q_OBJECT so explicitly pass in the header
        ../CADventory.h
)