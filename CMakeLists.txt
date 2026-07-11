cmake_minimum_required(VERSION 3.25)

set(CADVENTORY_VERSION 0.2.0)
project(CADventory VERSION ${CADVENTORY_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_VERBOSE_MAKEFILE ON)

# Add option for disabling gui in headless environments
option(CADVENTORY_WITH_GUI "Build with Qt Widgets/Gui" ON)

find_package(Threads REQUIRED)

# always needed qt
set(_qt_components Core Concurrent)
set(_qt_modules Qt6::Core Qt6::Concurrent)
if(CADVENTORY_WITH_GUI)
  # needed with Gui
  list(APPEND _qt_components Gui Widgets)
  list(APPEND _qt_modules Qt6::Widgets Qt6::Gui)
endif()

find_package(Qt6 REQUIRED COMPONENTS ${_qt_components})
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
  src/core/CADventory.cpp
  src/core/executeCommand.cpp
  src/core/main.cpp

  src/domain/jobqueue/FileJobQueue.cpp
  src/domain/jobqueue/QtJobServiceBase.cpp
  src/domain/jobqueue/JobManager.cpp
  src/domain/jobqueue/JobWorker.cpp
  src/domain/jobqueue/SQLJobQueue.cpp
  src/domain/jobqueue/SimpleFileLock.cpp
  src/domain/library/FilesystemIndexer.cpp
  src/domain/library/Library.cpp
  src/domain/library/LibraryBackup.cpp
  src/domain/library/LibraryIntegrity.cpp
  src/domain/llm/OllamaCLIService.cpp
  src/domain/model/Model.cpp
  src/domain/model/AuditLog.h
  src/domain/model/QtModelsListModel.cpp
  src/domain/model/ModelsManager.cpp
  src/domain/model/SQLModelRepository.cpp
  src/domain/model/SQLiteDB.cpp
  src/domain/model/AIModelTagging.cpp
  src/domain/model/ModelParser.cpp
  src/domain/processing/IndexingWorker.cpp
  src/domain/processing/ProcessGFiles.cpp
  src/domain/processing/ReportGeneratorWorker.cpp
  src/domain/processing/IDirectiveHandler.cpp
  src/domain/processing/sha1.cpp
)
set(HEADERS
  src/core/CADventory.h
  src/core/executeCommand.h

  src/domain/jobqueue/FileJobQueue.h
  src/domain/jobqueue/SQLJobQueue.h
  src/domain/jobqueue/SimpleFileLock.h
  src/domain/library/FilesystemIndexer.h
  src/domain/library/Library.h
  src/domain/library/LibraryBackup.h
  src/domain/library/LibraryIntegrity.h
  src/domain/llm/ILLMService.h
  src/domain/llm/OllamaCLIService.h
  src/domain/model/Model.h
  src/domain/model/ModelMetadata.h
  src/domain/model/ModelTypes.h
  src/domain/model/QtModelsListModel.h
  src/domain/model/ModelsManager.h
  src/domain/model/SQLModelRepository.h
  src/domain/model/SQLiteDB.h
  src/domain/model/ModelParser.h
  src/domain/model/AIModelTagging.h
  src/domain/processing/IndexingWorker.h
  src/domain/processing/ProcessGFiles.h
  src/domain/processing/ReportGeneratorWorker.h
  src/domain/processing/IDirectiveHandler.h
  src/domain/processing/DummyHandler.h
  src/domain/processing/ProcessHandler.h
  src/domain/processing/ThumbHandler.h
  src/domain/processing/sha1.h
)
set(UIS
  src/ui/UiShim.h
)

if(CADVENTORY_WITH_GUI)
  list(APPEND SRCS
    src/ui/filesystem/FileSystemModelWithCheckboxes.cpp
    src/ui/filesystem/FileSystemFilterProxyModel.cpp
    src/ui/filesystem/ModelFilterProxyModel.cpp
    src/ui/widgets/GeometryBrowserDialog.cpp
    src/ui/widgets/ModelCardDelegate.cpp
    src/ui/widgets/ModelView.cpp
  src/ui/windows/LibraryWindow.cpp
  src/ui/windows/AuditViewerDialog.cpp
  src/ui/windows/IntegrityViewerDialog.cpp
    src/ui/windows/MainWindow.cpp
    src/ui/windows/ReportGenerationWindow.cpp
    src/ui/windows/SettingWindow.cpp
    src/ui/windows/SplashDialog.cpp
  )
  list(APPEND HEADERS
    src/ui/filesystem/FileSystemFilterProxyModel.h
    src/ui/filesystem/FileSystemModelWithCheckboxes.h
    src/ui/filesystem/ModelFilterProxyModel.h
    src/ui/widgets/GeometryBrowserDialog.h
    src/ui/widgets/ModelCardDelegate.h
    src/ui/widgets/ModelView.h
    src/ui/windows/LibraryWindow.h
    src/ui/windows/AuditViewerDialog.h
    src/ui/windows/IntegrityViewerDialog.h
    src/ui/windows/MainWindow.h
    src/ui/windows/ReportGenerationWindow.h
    src/ui/windows/SettingWindow.h
    src/ui/windows/SplashDialog.h
  )
  list(APPEND UIS
    src/ui/widgets/modelview.ui

    src/ui/windows/librarywindow.ui
    src/ui/windows/mainwindow.ui
    src/ui/windows/reportgenerationwindow.ui
    src/ui/windows/SettingWindow.ui
    src/ui/windows/splash.ui
  )
endif()

set(CMAKE_AUTORCC ON)
# qt_add_executable automatically handles AUTOUIC, AUTOMOC, AUTORCC
qt_add_executable(cadventory WIN32
  ${SRCS}
  ${UIS}
  ${HEADERS}
  src/assets/images.qrc
)

# Expect .h files in all directories under src/
set(SRC_INCLUDE_DIRECTORIES
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/domain
    ${CMAKE_SOURCE_DIR}/src/domain/jobqueue
    ${CMAKE_SOURCE_DIR}/src/domain/library
    ${CMAKE_SOURCE_DIR}/src/domain/llm
    ${CMAKE_SOURCE_DIR}/src/domain/model
    ${CMAKE_SOURCE_DIR}/src/domain/processing
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/filesystem
    ${CMAKE_SOURCE_DIR}/src/ui/widgets
    ${CMAKE_SOURCE_DIR}/src/ui/windows
)
# Note dependencies and include directories
target_include_directories(cadventory PRIVATE ${SRC_INCLUDE_DIRECTORIES})

# export so we can use in code
target_compile_definitions(cadventory PUBLIC CADVENTORY_WITH_GUI=$<BOOL:${CADVENTORY_WITH_GUI}>)

# Qt links
target_link_libraries(cadventory PRIVATE ${_qt_modules})

# sqlite links
if (DEFINED CADVENTORY_USE_SQLITE)
  target_link_libraries(cadventory PRIVATE ${SQLITE_LIBRARY})
else()
  target_include_directories(cadventory PRIVATE ${sqlite_INCLUDE_DIR})
  add_dependencies(cadventory sqlite_static)
  target_link_libraries(cadventory PRIVATE sqlite_static)
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
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/config.h.in"
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

# Have Windows start a console when debugging
if (WIN32)
  target_link_options(cadventory PRIVATE
    "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
    "$<$<CONFIG:RelWithDebInfo>:/SUBSYSTEM:CONSOLE>"
    "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>"
  )
endif()

# Windows wants DLLs in the same folder as the app
if(WIN32)
  # Function to copy Qt DLLs for a given target
  function(copy_qt_dlls target)
    foreach(qt_module IN ITEMS ${_qt_modules})
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

  # function to copy brlcad DLLs for a target
  function(copy_brlcad_dlls target)
    set(brl_bin "${BRLCAD_ROOT}/bin")
    if (NOT EXISTS "${brl_bin}")
      message(WARNING "BRL-CAD bin dir not found: ${brl_bin}")
      return()
    endif()

    # lazy grab ALL dll's
    file(GLOB brl_dlls "${brl_bin}/*.dll")
    list(LENGTH brl_dlls brl_dll_count)
    if (brl_dll_count EQUAL 0)
        message(WARNING "No BRL-CAD DLLs found to copy from: ${brl_bin}")
    endif()
    message("Copying ${brl_dll_count} BRL-CAD DLLs after compile completes.")

    foreach(brl_dll IN LISTS brl_dlls)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${brl_dll}" $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying BRL-CAD DLL ${brl_dll} -> $<TARGET_FILE_DIR:${target}>"
      )
    endforeach()
  endfunction()

  # Call the function to copy the Qt DLLs for your target
  copy_qt_dlls(cadventory)
  # Call the function to copy all BRL DLLs for target
  copy_brlcad_dlls(cadventory)
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

# rpath settings for non-windows
if(NOT WIN32)
  # Function to get directory of an imported target's runtime library (Qt, BRL, etc.)
  function(_get_imported_dir OUT_VAR TGT)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      get_target_property(_loc ${TGT} IMPORTED_LOCATION_DEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
      get_target_property(_loc ${TGT} IMPORTED_LOCATION_RELWITHDEBINFO)
    elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
      get_target_property(_loc ${TGT} IMPORTED_LOCATION_MINSIZEREL)
    else()
      get_target_property(_loc ${TGT} IMPORTED_LOCATION_RELEASE)
    endif()

    # resolve loc
    if(NOT _loc)
      get_target_property(_loc ${TGT} IMPORTED_LOCATION)
    endif()
    if(NOT _loc)
      get_target_property(_loc ${TGT} LOCATION)
    endif()
    if(NOT _loc)
      message(FATAL_ERROR "Could not determine imported location for ${TGT}")
    endif()

    get_filename_component(_dir "${_loc}" DIRECTORY)
    set(${OUT_VAR} "${_dir}" PARENT_SCOPE)
  endfunction()

  # collect BRL-CAD targets
  set(RPATH_DIRS "")
  foreach(lib ${BRLCAD_LIBRARIES})
    if(IS_ABSOLUTE "${lib}")
      get_filename_component(_bdir "${lib}" DIRECTORY)
      list(APPEND RPATH_DIRS "${_bdir}")
    endif()
  endforeach()
  if (BRLCAD_ROOT AND EXISTS "${BRLCAD_ROOT}/lib")
    list(APPEND RPATH_DIRS "${BRLCAD_ROOT}/lib")
  endif()

  # collect qt targets
  foreach(mod IN LISTS _qt_modules)
    if(TARGET ${mod})
      _get_imported_dir(_qt_dir ${mod})
      list(APPEND RPATH_DIRS "${_qt_dir}")
    endif()
  endforeach()

  # dedupe
  list(REMOVE_DUPLICATES RPATH_DIRS)

  # use modern RUNPATH on ELF and allow $ORIGIN in build rpath
  if(UNIX AND NOT APPLE)
    target_link_options(cadventory PRIVATE -Wl,--enable-new-dtags)
  endif()
  set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

  # apple vs linux per-platform origin token
  if(APPLE)
    set(_ORIGIN_RPATH "@loader_path;@loader_path/../lib")
  else()
    set(_ORIGIN_RPATH "$ORIGIN;$ORIGIN/../lib")
  endif()

  # BUILD_RPATH: BRL-CAD + Qt + origin fallbacks (run from build tree)
  # INSTALL_RPATH: BRL-CAD + origin fallbacks (run after install)
  set(_BUILD_RPATH   "${RPATH_DIRS};${_ORIGIN_RPATH}")
  set(_INSTALL_RPATH "${RPATH_DIRS};${_ORIGIN_RPATH}")

  # apply to target
  set_target_properties(cadventory PROPERTIES
    BUILD_RPATH   "${_BUILD_RPATH}"
    INSTALL_RPATH "${_INSTALL_RPATH}"
  )
endif()

# Testing
enable_testing()
add_subdirectory(src/tests)
