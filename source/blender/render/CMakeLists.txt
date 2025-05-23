# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2006 Blender Foundation


set(INC
  .
  intern
  ../blenkernel
  ../blenlib
  ../blentranslation
  ../depsgraph
  ../draw
  ../gpu
  ../imbuf
  ../makesdna
  ../makesrna
  ../nodes
  ../sequencer
  ../simulation
  ../windowmanager
  ../../../intern/atomic
  ../../../intern/guardedalloc
  ../../../intern/mikktspace
  ../../../intern/mantaflow/extern
)

set(INC_SYS
)

set(SRC
  intern/bake.cc
  intern/engine.cc
  intern/initrender.cc
  intern/multires_bake.cc
  intern/pipeline.cc
  intern/render_result.cc
  intern/texture_image.c
  intern/texture_margin.cc
  intern/texture_pointdensity.c
  intern/texture_procedural.c
  intern/zbuf.c

  RE_bake.h
  RE_engine.h
  RE_multires_bake.h
  RE_pipeline.h
  RE_texture.h
  RE_texture_margin.h

  intern/pipeline.hh
  intern/render_result.h
  intern/render_types.h
  intern/texture_common.h
  intern/zbuf.h
)

set(LIB
)

if(WITH_PYTHON)
  add_definitions(-DWITH_PYTHON)
  list(APPEND INC
    ../python
  )
endif()

if(WITH_IMAGE_OPENEXR)
  list(APPEND LIB
    bf_imbuf_openexr
  )
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_FREESTYLE)
  list(APPEND INC
    ../freestyle
  )
  list(APPEND LIB
    bf_freestyle
  )
  add_definitions(-DWITH_FREESTYLE)
endif()

if(WITH_GAMEENGINE)
	add_definitions(-DWITH_GAMEENGINE)
endif()

blender_add_lib_nolist(bf_render "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
