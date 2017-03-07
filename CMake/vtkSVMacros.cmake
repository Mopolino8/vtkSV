# Copyright (c) 2014-2015 The Regents of the University of California.
# All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject
# to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
# OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#------------------------------------------------------------------------------
# Macro to add a module
macro(vtksv_add_module MODULE_NAME)
    set(options INSTALL_EXES)
    set(oneValueArgs EXECUTABLE_NAME)
    set(multiValueArgs SRCS HDRS LIBRARY_DEPENDS PACKAGE_DEPENDS)

    unset(vtksv_add_module_EXECUTABLE_NAME)

    cmake_parse_arguments("vtksv_add_module"
      "${options}"
      "${oneValueArgs}"
      "${multiValueArgs}" ${ARGN})

  #------------------------------------------------------------------------------
  # Set filter name and executable name
  set(vtk-module ${MODULE_NAME})
  if(vtksv_add_module_EXECUTABLE_NAME)
    set(exe ${vtksv_add_module_EXECUTABLE_NAME})
  endif()
  #------------------------------------------------------------------------------

  #------------------------------------------------------------------------------
  # Set filter name and executable name
  if(NOT vtksv_add_module_SRCS)
    message(ERROR "No SRCS provided for module ${vtk-module}")
  endif()
  if(NOT vtksv_add_module_HDRS)
    message(ERROR "No HDRS provided for module ${vtk-module}")
  endif()
  set(${vtk-module}_SRCS ${vtksv_add_module_SRCS})
  set(${vtk-module}_HDRS ${vtksv_add_module_HDRS})
  #------------------------------------------------------------------------------

  #------------------------------------------------------------------------------
  # Call vtk module setup
  set(${vtk-module}_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(${vtk-module}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  include(module_${vtk-module}.cmake)
  vtk_module_library(${vtk-module} ${${vtk-module}_SRCS})
  list(APPEND VTK_MODULES_ALL ${vtk-module})
  set(VTKSV_MODULES_ENABLED ${VTKSV_MODULES_ENABLED} ${vtk-module} CACHE INTERNAL "List of enabled modules")
  #------------------------------------------------------------------------------

  #------------------------------------------------------------------------------
  # Install
  if(vtksv_add_module_EXECUTABLE_NAME)
    add_executable(${exe} ${exe}.cxx ${${vtk-module}_SRCS})
    target_link_libraries(${exe} ${vtksv_add_module_LIBRARY_DEPENDS} ${vtksv_add_module_PACKAGE_DEPENDS})
  endif()
  if(vtksv_add_module_INSTALL_EXES)
    install(TARGETS ${exe}
      RUNTIME DESTINATION ${VTKSV_INSTALL_RUNTIME_DIR} COMPONENT Executables)
  endif()
  #------------------------------------------------------------------------------
endmacro()
