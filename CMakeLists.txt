cmake_minimum_required(VERSION 3.25)

project(CADventory VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_VERBOSE_MAKEFILE ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Concurrent)
qt_standard_project_setup()

# Flag adjustments for Qt requirements
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /permissive-")
endif()

# Support SQLite built from external or against system
if (DEFINED CADVENTORY_USE_SQLITE)
  find_package(SQLite3 REQUIRED)
  set(SQLITE_LIBRARY SQLite::SQLite3)
else()
  include(cmake/sqlite.cmake)
  set(SQLITE_LIBRARY sqlite_static)
endif()

# Import the static library that was built (assuming sqlite.cmake defines sqlite_STATIC_LIBRARIES and sqlite_INCLUDE_DIR)
if(NOT DEFINED CADVENTORY_USE_SQLITE)
  add_library(sqlite_static IMPORTED STATIC GLOBAL)
  add_dependencies(sqlite_static sqlite)
  set_target_properties(sqlite_static PROPERTIES
    IMPORTED_LOCATION "${sqlite_STATIC_LIBRARIES}"
  )
endif()

# All warnings for compliance
if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -pedantic)
endif()

set(SRCS
  src/CADventory.cpp
  src/FilesystemIndexer.cpp
  src/MainWindow.cpp
  src/SplashDialog.cpp
  src/Model.cpp
  src/Library.cpp
  src/LibraryWindow.cpp
  src/ProcessGFiles.cpp
  src/IndexingWorker.cpp
  src/ModelCardDelegate.cpp
  src/ModelFilterProxyModel.cpp
  src/GeometryBrowserDialog.cpp
  src/ReportGenerationWindow.cpp
  src/main.cpp
  src/ReportGeneratorWorker.cpp
  src/SettingWindow.cpp
  src/ModelView.cpp
  src/FileSystemModelWithCheckboxes.cpp
  src/FileSystemFilterProxyModel.cpp
  src/ModelTagging.cpp
  src/ModelParser.cpp
  src/executeCommand.cpp
)

set(UIS
  src/librarywindow.ui
  src/mainwindow.ui
  src/splash.ui
  src/reportgenerationwindow.ui
  src/SettingWindow.ui
  src/modelview.ui
)

set(HEADERS
  src/LibraryWindow.h
  src/MainWindow.h
  src/Library.h
  src/Model.h
  src/ProcessGFiles.h
  src/IndexingWorker.h
  src/ModelCardDelegate.h
  src/ModelFilterProxyModel.h
  src/GeometryBrowserDialog.h
  src/ReportGenerationWindow.h
  src/ReportGeneratorWorker.h
  src/SettingWindow.h
  src/FileSystemModelWithCheckboxes.h
  src/FileSystemFilterProxyModel.h
  src/ModelTagging.h
  src/ModelParser.h
  src/executeCommand.h
  src/ModelMetadata.h
)

set(CMAKE_AUTORCC ON)
# qt_add_executable automatically handles AUTOUIC, AUTOMOC, AUTORCC
qt_add_executable(cadventory WIN32
  ${SRCS}
  ${UIS}
  ${HEADERS}
  images.qrc
)

# Note dependencies and include directories
target_include_directories(cadventory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (DEFINED CADVENTORY_USE_SQLITE)
  target_link_libraries(cadventory PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Concurrent ${SQLITE_LIBRARY})
else()
  target_include_directories(cadventory PRIVATE ${sqlite_INCLUDE_DIR})
  add_dependencies(cadventory sqlite_static)
  target_link_libraries(cadventory PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Concurrent sqlite_static)
endif()

# --- Begin BRL-CAD Configuration ---

# Add the directory containing FindBRLCAD.cmake to CMAKE_MODULE_PATH
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Optionally set BRLCAD_ROOT to your BRL-CAD installation directory
if(NOT BRLCAD_ROOT)
  set(BRLCAD_ROOT "" CACHE PATH "Path to BRL-CAD installation")
endif()

# Find BRL-CAD using the provided FindBRLCAD.cmake module
find_package(BRLCAD REQUIRED)

if(BRLCAD_FOUND)
  message(STATUS "BRL-CAD found.")
  message(STATUS "BRLCAD_LIBRARIES: ${BRLCAD_LIBRARIES}")
  message(STATUS "BRLCAD_INCLUDE_DIRS: ${BRLCAD_INCLUDE_DIRS}")
  # Include the BRL-CAD include 
  target_compile_definitions(cadventory PRIVATE RT_DLL_IMPORTS)
  target_include_directories(cadventory PRIVATE ${BRLCAD_INCLUDE_DIRS})
  # Link against BRLCAD::BRLCAD
  target_link_libraries(cadventory PRIVATE BRLCAD::BRLCAD)
else()
  message(FATAL_ERROR "BRL-CAD not found.")
endif()

# Find the BRL-CAD executables
set(MGED_EXECUTABLE "${BRLCAD_ROOT}/bin/mged${CMAKE_EXECUTABLE_SUFFIX}")
set(RT_EXECUTABLE "${BRLCAD_ROOT}/bin/rt${CMAKE_EXECUTABLE_SUFFIX}")
set(GIST_EXECUTABLE "${BRLCAD_ROOT}/bin/gist${CMAKE_EXECUTABLE_SUFFIX}")

if(NOT EXISTS "${MGED_EXECUTABLE}" OR NOT EXISTS "${RT_EXECUTABLE}" OR NOT EXISTS "${GIST_EXECUTABLE}")
  message(FATAL_ERROR "Could not find BRL-CAD executables. Please ensure BRLCAD_ROOT is set correctly.")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

# Include the generated config.h
target_include_directories(cadventory PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- End BRL-CAD Configuration ---

# Deployment on Windows needs DLLs copied
install(TARGETS cadventory
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES splash.png
  # Eventually should go to DATADIR
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES doc/CADventory.docx
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# Windows wants DLLs in the same folder as the app
if(WIN32)
  # Function to copy Qt DLLs for a given target
  function(copy_qt_dlls target)
    foreach(qt_module IN ITEMS Qt6::Core Qt6::Widgets Qt6::Gui)
      if(CMAKE_BUILD_TYPE MATCHES "Debug")
        get_target_property(lib_location ${qt_module} IMPORTED_LOCATION_DEBUG)
      else()
        get_target_property(lib_location ${qt_module} IMPORTED_LOCATION_RELEASE)
      endif()

      if(lib_location)
        message("Copying DLLs from ${lib_location} after compile completes.")

        # Assume DLL is in dir relative to the .lib file
        get_filename_component(lib_dir ${lib_location} DIRECTORY)
        get_filename_component(lib_name ${lib_location} NAME_WE)

        # Remove 'lib' prefix if present
        string(REGEX REPLACE "^lib" "" dll_name ${lib_name})
        set(dll_path "${lib_dir}/../bin/${dll_name}.dll")

        # Copy DLL to the target output build dir
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll_path}" $<TARGET_FILE_DIR:${target}>
          COMMENT "Copying ${dll_path} to $<TARGET_FILE_DIR:${target}>"
        )

        # Copy DLL to the install dir
        install(FILES ${dll_path}
          DESTINATION $<TARGET_FILE_DIR:${target}>
        )
      else()
        message(WARNING "DLL for ${qt_module} not found.")
      endif()
    endforeach()
  endfunction()

  # Call the function to copy the Qt DLLs for your target
  copy_qt_dlls(cadventory)
endif()

# Also copy Qt plugins
if(WIN32)
  set(QT_PLUGINS_DIR "${Qt6_DIR}/../../../plugins")
  set(QT_REQUIRED_PLUGINS platforms imageformats)

  foreach(plugin ${QT_REQUIRED_PLUGINS})
    set(PLUGIN_SRC_DIR "${QT_PLUGINS_DIR}/${plugin}")
    set(PLUGIN_DST_DIR "$<TARGET_FILE_DIR:cadventory>/${plugin}")

    # Copy the plugin directory to the build dir
    add_custom_command(TARGET cadventory POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${PLUGIN_DST_DIR}
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${PLUGIN_SRC_DIR} ${PLUGIN_DST_DIR}
      COMMENT "Copying Qt plugin: ${plugin}\n"
    )

    # Install plugin directory to the install dir
    install(DIRECTORY ${PLUGIN_DST_DIR}
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endforeach()
endif()

# Testing
# enable_testing()
# add_subdirectory(src/tests)