#########################################################################
#
# BRL-CAD
#
# Copyright (c) 1997-2011 United States Government as represented by
# the U.S. Army Research Laboratory.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# version 2.1 as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this file; see the file named COPYING for more
# information.
#
#########################################################################
# @file FindBRLCAD.cmake
#
#  Try to find brlcad libraries.
#  Once done, this will define:
#
#   BRLCAD_FOUND - system has BRL-CAD
#   BRLCAD_VERSION - the BRL-CAD version string
#   BRLCAD_INCLUDE_DIRS - the BRL-CAD include directories
#   BRLCAD_LIBRARIES - link these to use the BRL-CAD Libraries
#
#   BRLCAD_ANALYZE_LIBRARY - BRL-CAD Analysis library
#   BRLCAD_BG_LIBRARY - BRL-CAD Geometry Algorithms library
#   BRLCAD_BN_LIBRARY - BRL-CAD Numerical library
#   BRLCAD_BV_LIBRARY - BRL-CAD View management library
#   BRLCAD_BREP_LIBRARY - BRL-CAD NURBS Brep Algorithms library
#   BRLCAD_BU_LIBRARY - BRL-CAD Utility library
#   BRLCAD_DM_LIBRARY - BRL-CAD Display Manager/Framebuffer library
#   BRLCAD_FFT_LIBRARY - BRL-CAD FFT library
#   BRLCAD_GCV_LIBRARY - BRL-CAD Geometry Conversion library
#   BRLCAD_GED_LIBRARY - BRL-CAD Geometry Editing library
#   BRLCAD_ICV_LIBRARY - BRL-CAD Image Conversion library
#   BRLCAD_NMG_LIBRARY - BRL-CAD N-Manifold Generation library
#   BRLCAD_OPTICAL_LIBRARY - BRL-CAD optical library
#   BRLCAD_PKG_LIBRARY - BRL-CAD libpkg
#   BRLCAD_QTCAD_LIBRARY - BRL-CAD libqtcad (currently optional)
#   BRLCAD_RENDER_LIBRARY - librender
#   BRLCAD_RT_LIBRARY - BRL-CAD Raytracing library
#   BRLCAD_TCLCAD_LIBRARY - libtclcad
#   BRLCAD_WDB_LIBRARY - BRL-CAD Write Database library
#
#   NOTE: customized version needed for BRL-CAD NURBS support
#   BRLCAD_OPENNURBS_LIBRARY - openNURBS library
#
#########################################################################

if (NOT DEFINED BRLCAD_ROOT)
  set(BRLCAD_ROOT "$ENV{BRLCAD_ROOT}")
endif (NOT DEFINED BRLCAD_ROOT)

# First, find the install directories.
if(NOT BRLCAD_ROOT)

  # Try looking for BRL-CAD's brlcad-config - it should give us
  # a location for bin, and the parent will be the root dir
  if(NOT BRLCAD_ROOT)
    find_program(BRLCAD_CONFIGEXE brlcad-config)
    if(BRLCAD_CONFIGEXE)
      execute_process(COMMAND brlcad-config --prefix OUTPUT_VARIABLE BRLCAD_ROOT)
    endif(BRLCAD_CONFIGEXE)
  endif(NOT BRLCAD_ROOT)

  # If that didn't work, see if we can find brlcad-config and use the parent path
  if(NOT BRLCAD_ROOT)
    find_path(BRLCAD_BIN_DIR brlcad-config)
    if(BRLCAD_BIN_DIR)
      get_filename_component(BRLCAD_ROOT ${BRLCAD_BIN_DIR} PATH)
    endif(BRLCAD_BIN_DIR)
  endif(NOT BRLCAD_ROOT)

  # Look for headers if we come up empty with brlcad-config
  if(NOT BRLCAD_ROOT)
    file(GLOB CAD_DIRS LIST_DIRECTORIES TRUE "/usr/brlcad/*")
    foreach(CDIR ${CAD_DIRS})
      if (IS_DIRECTORY ${CDIR})
        list(APPEND BRLCAD_HEADERS_DIR_CANDIDATES "${CDIR}/include/brlcad")
      endif (IS_DIRECTORY ${CDIR})
    endforeach(CDIR ${CAD_DIRS})
    file(GLOB CAD_DIRS_LOCAL LIST_DIRECTORIES TRUE "/usr/local/brlcad/*")
    foreach(CDIR ${CAD_DIRS_LOCAL})
      if (IS_DIRECTORY ${CDIR})
        list(APPEND BRLCAD_HEADERS_DIR_CANDIDATES "${CDIR}/include/brlcad")
      endif (IS_DIRECTORY ${CDIR})
    endforeach(CDIR ${CAD_DIRS_LOCAL})

    # If there are multiple installed BRL-CAD versions present,
    # we want the newest
    if (${CMAKE_VERSION} VERSION_GREATER "3.17.99")
      list(SORT BRLCAD_HEADERS_DIR_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    endif (${CMAKE_VERSION} VERSION_GREATER "3.17.99")

    # Look for brlcad.h
    foreach(CDIR ${BRLCAD_HEADERS_DIR_CANDIDATES})
      if (NOT BRLCAD_ROOT)
        find_path(BRLCAD_HEADERS_DIR NAMES brlcad.h PATHS ${CDIR})
        if(BRLCAD_HEADERS_DIR)
          get_filename_component(BRLCAD_INC_DIR ${BRLCAD_HEADERS_DIR} PATH)
          get_filename_component(BRLCAD_ROOT ${BRLCAD_INC_DIR} PATH)
        endif(BRLCAD_HEADERS_DIR)
      endif (NOT BRLCAD_ROOT)
    endforeach(CDIR ${BRLCAD_HEADERS_DIR_CANDIDATES})
  endif(NOT BRLCAD_ROOT)

endif(NOT BRLCAD_ROOT)

#Find include directories
if(NOT BRLCAD_HEADERS_DIR)
  find_path(BRLCAD_HEADERS_DIR NAMES brlcad.h HINTS ${BRLCAD_ROOT} PATH_SUFFIXES include/brlcad)
  get_filename_component(BRLCAD_HEADERS_PARENT_DIR ${BRLCAD_HEADERS_DIR} PATH)
endif(NOT BRLCAD_HEADERS_DIR)
find_path(BRLCAD_OPENNURBS_HEADERS_DIR NAMES opennurbs.h HINTS ${BRLCAD_ROOT} PATH_SUFFIXES include/openNURBS include/opennurbs include/OpenNURBS)
set(BRLCAD_INCLUDE_DIR ${BRLCAD_HEADERS_PARENT_DIR} ${BRLCAD_HEADERS_DIR} ${BRLCAD_OPENNURBS_HEADERS_DIR})

#Find library directory
if (WIN32)
  # Windows is a rather odd special case - it's shared library suffix is "dll",
  # but what we want for this purpose is the "lib" files
  find_path(BRLCAD_LIB_DIR "libbu.lib" PATHS ${BRLCAD_ROOT} PATH_SUFFIXES lib libs)
else (WIN32)
  find_path(BRLCAD_LIB_DIR "libbu${CMAKE_SHARED_LIBRARY_SUFFIX}" PATHS ${BRLCAD_ROOT} PATH_SUFFIXES lib libs)
endif (WIN32)

#Find binary directory
if(NOT BRLCAD_BIN_DIR)
  find_path(BRLCAD_BIN_DIR brlcad-config PATHS ${BRLCAD_ROOT} PATH_SUFFIXES bin)
endif(NOT BRLCAD_BIN_DIR)

#Attempt to get brlcad version.
if(NOT BRLCAD_CONFIGEXE)
  find_program(BRLCAD_CONFIGEXE brlcad-config PATHS ${BRLCAD_ROOT} PATH_SUFFIXES bin)
endif(NOT BRLCAD_CONFIGEXE)
if(BRLCAD_CONFIGEXE)
  execute_process(COMMAND ${BRLCAD_CONFIGEXE} --version OUTPUT_VARIABLE BRLCAD_VERSION)
  # Don't try to strip if BRLCAD_VERSION is empty - CMake doesn't like it and will
  # return an error.
  if (NOT "${BRLCAD_VERSION}" STREQUAL "")
    string(STRIP ${BRLCAD_VERSION} BRLCAD_VERSION)
    if(BRLCAD_VERSION)
      string(REGEX REPLACE "([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" BRLCAD_VERSION_MAJOR "${BRLCAD_VERSION}")
      string(REGEX REPLACE "[0-9]+\\.([0-9]+)\\.[0-9]+" "\\1" BRLCAD_VERSION_MINOR "${BRLCAD_VERSION}")
      string(REGEX REPLACE "[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" BRLCAD_VERSION_PATCH "${BRLCAD_VERSION}")
    endif(BRLCAD_VERSION)
  endif (NOT "${BRLCAD_VERSION}" STREQUAL "")
endif(BRLCAD_CONFIGEXE)
if(NOT BRLCAD_VERSION AND BRLCAD_HEADERS_DIR)
   # If we don't have a working brlcad-config, we can try reading the info
   # directly from brlcad_version.h
   if (EXISTS "${BRLCAD_HEADERS_DIR}/brlcad_version.h")
     file(STRINGS "${BRLCAD_HEADERS_DIR}/brlcad_version.h" VINFO)
     foreach(vline ${VINFO})
       if ("${vline}" MATCHES "define BRLCAD_VERSION_MAJOR [0-9]+")
         string(REGEX REPLACE ".*define BRLCAD_VERSION_MAJOR " "" BRLCAD_VERSION_MAJOR ${vline})
         string(STRIP ${BRLCAD_VERSION_MAJOR} BRLCAD_VERSION_MAJOR)
       endif ("${vline}" MATCHES "define BRLCAD_VERSION_MAJOR [0-9]+")
       if ("${vline}" MATCHES "define BRLCAD_VERSION_MINOR [0-9]+")
         string(REGEX REPLACE ".*define BRLCAD_VERSION_MINOR " "" BRLCAD_VERSION_MINOR ${vline})
         string(STRIP ${BRLCAD_VERSION_MINOR} BRLCAD_VERSION_MINOR)
       endif ("${vline}" MATCHES "define BRLCAD_VERSION_MINOR [0-9]+")
       if ("${vline}" MATCHES "define BRLCAD_VERSION_PATCH [0-9]+")
         string(REGEX REPLACE ".*define BRLCAD_VERSION_PATCH " "" BRLCAD_VERSION_PATCH ${vline})
         string(STRIP ${BRLCAD_VERSION_PATCH} BRLCAD_VERSION_PATCH)
       endif ("${vline}" MATCHES "define BRLCAD_VERSION_PATCH [0-9]+")
     endforeach(vline ${VINFO})
     set(BRLCAD_VERSION "${BRLCAD_VERSION_MAJOR}.${BRLCAD_VERSION_MINOR}.${BRLCAD_VERSION_PATCH}")
   endif (EXISTS "${BRLCAD_HEADERS_DIR}/brlcad_version.h")
endif(NOT BRLCAD_VERSION AND BRLCAD_HEADERS_DIR)
if(NOT BRLCAD_VERSION)
  set(BRLCAD_VERSION_FOUND FALSE)
else(NOT BRLCAD_VERSION)
  set(BRLCAD_VERSION_MAJOR ${BRLCAD_VERSION_MAJOR} CACHE STRING "BRL-CAD major version")
  set(BRLCAD_VERSION_MINOR ${BRLCAD_VERSION_MINOR} CACHE STRING "BRL-CAD minor version")
  set(BRLCAD_VERSION_PATCH ${BRLCAD_VERSION_PATCH} CACHE STRING "BRL-CAD patch version")
  set(BRLCAD_VERSION ${BRLCAD_VERSION} CACHE STRING "BRL-CAD version")
  set(BRLCAD_VERSION_FOUND TRUE)
endif(NOT BRLCAD_VERSION)

##########################################################################
# Search for BRL-CAD's own libraries
set(BRLCAD_REQ_LIB_NAMES
  analyze
  bg
  bn
  bv
  brep
  bu
  dm
  fft
  gcv
  ged
  icv
  nmg
  optical
  pkg
  render
  rt
  wdb
  )

SET(BRLCAD_REQ_LIBS)
foreach(brl_lib ${BRLCAD_REQ_LIB_NAMES})
  string(TOUPPER ${brl_lib} LIBCORE)
  find_library(BRLCAD_${LIBCORE}_LIBRARY NAMES ${brl_lib} lib${brl_lib} PATHS ${BRLCAD_LIB_DIR} NO_SYSTEM_PATH)
  set(BRLCAD_REQ_LIBS ${BRLCAD_REQ_LIBS} BRLCAD_${LIBCORE}_LIBRARY)
  if(BRLCAD_${LIBCORE}_LIBRARY)
    set(BRLCAD_LIBRARIES ${BRLCAD_LIBRARIES} ${BRLCAD_${LIBCORE}_LIBRARY})
  else(BRLCAD_${LIBCORE}_LIBRARY)
    set(BRLCAD_LIBRARIES_NOTFOUND ${BRLCAD_LIBRARIES_NOTFOUND} ${brl_lib})
  endif(BRLCAD_${LIBCORE}_LIBRARY)
endforeach(brl_lib ${BRLCAD_REQ_LIB_NAMES})

# Optional libraries - we will still return a successful
# search if these aren't here
set(BRLCAD_OPT_LIB_NAMES
  tclcad
  qtcad
  )
foreach(brl_lib ${BRLCAD_OPT_LIB_NAMES})
  string(TOUPPER ${brl_lib} LIBCORE)
  find_library(BRLCAD_${LIBCORE}_LIBRARY NAMES ${brl_lib} lib${brl_lib} PATHS ${BRLCAD_LIB_DIR} NO_SYSTEM_PATH)
  if(BRLCAD_${LIBCORE}_LIBRARY)
    set(BRLCAD_LIBRARIES ${BRLCAD_LIBRARIES} ${BRLCAD_${LIBCORE}_LIBRARY})
  endif(BRLCAD_${LIBCORE}_LIBRARY)
endforeach(brl_lib ${BRLCAD_OPT_LIB_NAMES})

# Then, look for customized src/other libraries that we need
# local versions of

set(BRL-CAD_SRC_OTHER_REQUIRED
  openNURBS
  )

foreach(ext_lib ${BRL-CAD_LIBS_SEARCH_LIST})
  string(TOUPPER ${ext_lib} LIBCORE)
  find_library(BRLCAD_${LIBCORE}_LIBRARY NAMES ${ext_lib} lib${ext_lib} PATHS ${BRLCAD_LIB_DIR} NO_SYSTEM_PATH)
  set(BRLCAD_REQ_LIBS ${BRLCAD_REQ_LIBS} BRLCAD_${LIBCORE}_LIBRARY)
  if(BRLCAD_${LIBCORE}_LIBRARY)
    set(BRLCAD_LIBRARIES ${BRLCAD_LIBRARIES} ${BRLCAD_${LIBCORE}_LIBRARY})
  else(BRLCAD_${LIBCORE}_LIBRARY)
    set(BRLCAD_LIBRARIES_NOTFOUND ${BRLCAD_LIBRARIES_NOTFOUND} ${ext_lib})
  endif(BRLCAD_${LIBCORE}_LIBRARY)
endforeach(ext_lib ${BRL-CAD_LIBS_SEARCH_LIST})

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(BRLCAD
  FOUND_VAR BRLCAD_FOUND
  REQUIRED_VARS ${BRLCAD_REQ_LIBS} BRLCAD_INCLUDE_DIR
  VERSION_VAR BRLCAD_VERSION
  FAIL_MESSAGE "\nBRL-CAD not found.  Try setting BRLCAD_ROOT to the directory containing the bin, include, and lib dirs (for example, -DBRLCAD_ROOT=/usr/brlcad/rel-7.32.4)\n"
  )

if(BRLCAD_FOUND)
  set(BRLCAD_INCLUDE_DIRS ${BRLCAD_INCLUDE_DIR} CACHE STRING "BRL-CAD include directories")
  set(BRLCAD_LIBRARIES ${BRLCAD_LIBRARIES} CACHE STRING "BRL-CAD libraries")
  # Set up a "modern" CMake target to make it easer for
  # client codes to reference the BRL-CAD libs.  See
  # https://stackoverflow.com/a/48397346/2037687
  set(libtargets)
  foreach(brl_lib ${BRLCAD_REQ_LIB_NAMES})
    string(TOUPPER ${brl_lib} LIBCORE)
    add_library(BRLCAD::${LIBCORE} UNKNOWN IMPORTED)
    set_target_properties(BRLCAD::${LIBCORE} PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${BRLCAD_INCLUDE_DIR}"
      IMPORTED_LOCATION ${BRLCAD_${LIBCORE}_LIBRARY}
      IMPORTED_LOCATION_DEBUG ${BRLCAD_${LIBCORE}_LIBRARY})
    set(libtargets ${libtargets} BRLCAD::${LIBCORE})
  endforeach(brl_lib ${BRL-CAD_LIBS_SEARCH_LIST})
  # For the optional libs, add them IFF they are present
  foreach(brl_lib ${BRLCAD_OPT_LIB_NAMES})
    string(TOUPPER ${brl_lib} LIBCORE)
    if (BRLCAD_${LIBCORE}_LIBRARY)
      add_library(BRLCAD::${LIBCORE} UNKNOWN IMPORTED)
      set_target_properties(BRLCAD::${LIBCORE} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BRLCAD_INCLUDE_DIR}"
        IMPORTED_LOCATION ${BRLCAD_${LIBCORE}_LIBRARY}
        IMPORTED_LOCATION_DEBUG ${BRLCAD_${LIBCORE}_LIBRARY})
      set(libtargets ${libtargets} BRLCAD::${LIBCORE})
    endif (BRLCAD_${LIBCORE}_LIBRARY)
  endforeach(brl_lib ${BRLCAD_OPT_LIB_NAMES})
  # Overall "kitchen sink" target
  add_library(BRLCAD::BRLCAD INTERFACE IMPORTED)
  set_property(TARGET BRLCAD::BRLCAD PROPERTY
    INTERFACE_LINK_LIBRARIES ${libtargets})
endif()


# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
