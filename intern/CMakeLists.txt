# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2006 Blender Foundation

# add_subdirectory(atomic)  # header only
add_subdirectory(atomic)
add_subdirectory(clog)
add_subdirectory(ghost)
add_subdirectory(guardedalloc)
add_subdirectory(libmv)
add_subdirectory(memutil)
add_subdirectory(opencolorio)
add_subdirectory(opensubdiv)
add_subdirectory(mikktspace)
add_subdirectory(eigen)
add_subdirectory(sky)

if(WITH_GAMEENGINE_DECKLINK)
	add_subdirectory(decklink)
endif()

if (WITH_GAMEENGINE_BPPLAYER)
  add_subdirectory(spindle)
endif()

if(WITH_AUDASPACE)
  add_subdirectory(audaspace)
endif()

if(WITH_MOD_REMESH)
  add_subdirectory(dualcon)
endif()

if(WITH_IK_SOLVER)
  add_subdirectory(iksolver)
endif()

if(WITH_IK_ITASC)
  add_subdirectory(itasc)
endif()

if(WITH_GAMEENGINE)
	add_subdirectory(moto)
endif()

if(WITH_CYCLES)
  add_subdirectory(cycles)
endif()

if(WITH_INTERNATIONAL)
  add_subdirectory(locale)
endif()

if(WITH_BULLET)
  add_subdirectory(rigidbody)
endif()

# only windows needs utf16 converter
# gpudirect is a runtime interface to the nVidia's DVP driver, only for windows
if(WIN32)
	add_subdirectory(utfconv)
	add_subdirectory(gpudirect)
endif()

if(WITH_MOD_FLUID)
  add_subdirectory(mantaflow)
endif()

if(WITH_OPENVDB)
  add_subdirectory(openvdb)
endif()

if(WITH_QUADRIFLOW)
  add_subdirectory(quadriflow)
endif()

if(WITH_CODEC_FFMPEG)
  add_subdirectory(ffmpeg)
endif()

if(UNIX AND NOT APPLE)
  add_subdirectory(libc_compat)
endif()

if(WITH_RENDERDOC)
  add_subdirectory(renderdoc_dynload)
endif()

if(UNIX AND NOT APPLE)
  # Important this comes after "ghost" as it uses includes defined by GHOST's CMake.
  if(WITH_GHOST_WAYLAND AND WITH_GHOST_WAYLAND_DYNLOAD)
    add_subdirectory(wayland_dynload)
  endif()
endif()
