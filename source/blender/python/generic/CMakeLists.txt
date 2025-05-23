# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  .
  ../../blenkernel
  ../../blenlib
  ../../gpu
  ../../makesdna
  ../../makesrna
  ../../../../intern/clog
  ../../../../intern/guardedalloc
)

set(INC_SYS
  ${Epoxy_INCLUDE_DIRS}
  ${PYTHON_INCLUDE_DIRS}
)

set(SRC
  bgl.c
  bl_math_py_api.c
  blf_py_api.c
  bpy_threads.c
  bpy_internal_import.c
  idprop_py_api.c
  idprop_py_ui_api.c
  imbuf_py_api.c
  py_capi_rna.c
  py_capi_utils.c

  bgl.h
  bl_math_py_api.h
  blf_py_api.h
  bpy_internal_import.h
  idprop_py_api.h
  idprop_py_ui_api.h
  imbuf_py_api.h
  py_capi_rna.h
  py_capi_utils.h

  # header-only
  python_utildefines.h
)

set(LIB
  ${Epoxy_LIBRARIES}
  ${PYTHON_LINKFLAGS}
  ${PYTHON_LIBRARIES}
)

if(WITH_PYTHON_MODULE)
  add_definitions(-DWITH_PYTHON_MODULE)
endif()

blender_add_lib(bf_python_ext "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
