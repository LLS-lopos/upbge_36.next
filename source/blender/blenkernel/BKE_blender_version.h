/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "BLI_utildefines.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 * \ingroup bke
 */

/**
 * The lines below use regex from scripts to extract their values,
 * Keep this in mind when modifying this file and keep this comment above the defines.
 *
 * \note Use #STRINGIFY() rather than defining with quotes.
 */

/* Blender major and minor version. */
#define BLENDER_VERSION 306

#define UPBGE_VERSION 306

/* Blender patch version for bugfix releases. */
#define BLENDER_VERSION_PATCH 20
/** Blender release cycle stage: alpha/beta/rc/release. */
#define BLENDER_VERSION_CYCLE release

#define UPBGE_VERSION_PATCH 2
/** alpha/beta/rc/release, docs use this. */
#define UPBGE_VERSION_CYCLE release

/* Blender file format version. */
#define BLENDER_FILE_VERSION BLENDER_VERSION
#define BLENDER_FILE_SUBVERSION 11

/* UPBGE file format version. */
#define UPBGE_FILE_VERSION UPBGE_VERSION
#define UPBGE_FILE_SUBVERSION 0

/* Minimum Blender version that supports reading file written with the current
 * version. Older Blender versions will test this and cancel loading the file, showing a warning to
 * the user.
 *
 * See https://wiki.blender.org/wiki/Process/Compatibility_Handling for details. */
#define BLENDER_FILE_MIN_VERSION 303
#define BLENDER_FILE_MIN_SUBVERSION 06

/** User readable version string. */
const char *BKE_blender_version_string(void);
const char *BKE_upbge_version_string(void);

/* Returns true when version cycle is alpha, otherwise (beta, rc) returns false. */
bool BKE_blender_version_is_alpha(void);

/** Fill in given string buffer with user-readable formated file version and subversion (if
 * provided).
 *
 * \param str_buff a char buffer where the formated string is written, minimal recommended size is
 * 8, or 16 if subversion is provided.
 *
 * \param file_subversion the file subversion, if given value < 0, it is ignored, and only the
 * `file_version` is used. */
void BKE_blender_version_blendfile_string_from_values(char *str_buff,
                                                      const size_t str_buff_len,
                                                      const short file_version,
                                                      const short file_subversion);

#ifdef __cplusplus
}
#endif
