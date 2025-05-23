/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2007 Blender Foundation */

/** \file
 * \ingroup wm
 *
 * Manage initializing resources and correctly shutting down.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#include "DNA_genfile.h"
#include "DNA_scene_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_timer.h"
#include "BLI_utildefines.h"

#include "BLO_undofile.h"
#include "BLO_writefile.h"

#include "BKE_blender.h"
#include "BKE_blendfile.h"
#include "BKE_callbacks.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_image.h"
#include "BKE_keyconfig.h"
#include "BKE_lib_remap.h"
#include "BKE_main.h"
#include "BKE_mball_tessellate.h"
#include "BKE_node.hh"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_sound.h"
#include "BKE_vfont.h"

#include "BKE_addon.h"
#include "BKE_appdir.h"
#include "BKE_mask.h"     /* free mask clipboard */
#include "BKE_material.h" /* BKE_material_copybuf_clear */
#include "BKE_studiolight.h"
#include "BKE_subdiv.h"
#include "BKE_tracking.h" /* free tracking clipboard */

#include "RE_engine.h"
#include "RE_pipeline.h" /* RE_ free stuff */

#include "SEQ_clipboard.h" /* free seq clipboard */

#include "IMB_thumbs.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#  include "BPY_extern_python.h"
#  include "BPY_extern_run.h"
#endif

#ifdef WITH_GAMEENGINE
#  include "LA_SystemCommandLine.h"
#endif

#include "GHOST_C-api.h"
#include "GHOST_Path-api.hh"

#include "RNA_define.h"

#include "WM_api.h"
#include "WM_message.h"
#include "WM_types.h"

#include "wm.h"
#include "wm_cursors.h"
#include "wm_event_system.h"
#include "wm_files.h"
#include "wm_platform_support.h"
#include "wm_surface.h"
#include "wm_window.h"

#include "ED_anim_api.h"
#include "ED_armature.h"
#include "ED_asset.h"
#include "ED_gpencil_legacy.h"
#include "ED_keyframes_edit.h"
#include "ED_keyframing.h"
#include "ED_node.h"
#include "ED_render.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_undo.h"
#include "ED_util.h"
#include "ED_view3d.h"

#include "BLF_api.h"
#include "BLT_lang.h"
#include "UI_interface.h"
#include "UI_resources.h"

#include "GPU_context.h"
#include "GPU_init_exit.h"
#include "GPU_material.h"

#include "COM_compositor.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DRW_engine.h"

CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_OPERATORS, "wm.operator");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_HANDLERS, "wm.handler");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_EVENTS, "wm.event");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_KEYMAPS, "wm.keymap");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_TOOLS, "wm.tool");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_MSGBUS_PUB, "wm.msgbus.pub");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_MSGBUS_SUB, "wm.msgbus.sub");

static void wm_init_reports(bContext *C)
{
  ReportList *reports = CTX_wm_reports(C);

  BLI_assert(!reports || BLI_listbase_is_empty(&reports->list));

  BKE_reports_init(reports, RPT_STORE);
}
static void wm_free_reports(wmWindowManager *wm)
{
  BKE_reports_clear(&wm->reports);
}

static bool wm_start_with_console = false;

void WM_init_state_start_with_console_set(bool value)
{
  wm_start_with_console = value;
}

/**
 * Since we cannot know in advance if we will require the draw manager
 * context when starting blender in background mode (specially true with
 * scripts) we defer the ghost initialization the most as possible
 * so that it does not break anything that can run in headless mode (as in
 * without display server attached).
 */
static bool opengl_is_init = false;

void WM_init_opengl(void)
{
  /* Must be called only once. */
  BLI_assert(opengl_is_init == false);

  if (G.background) {
    /* Ghost is still not initialized elsewhere in background mode. */
    wm_ghost_init_background();
  }

  if (!GPU_backend_supported()) {
    return;
  }

  /* Needs to be first to have an OpenGL context bound. */
  DRW_opengl_context_create();

  GPU_init();

  GPU_pass_cache_init();

  opengl_is_init = true;
}

static void sound_jack_sync_callback(Main *bmain, int mode, double time)
{
  /* Ugly: Blender doesn't like it when the animation is played back during rendering. */
  if (G.is_rendering) {
    return;
  }

  wmWindowManager *wm = static_cast<wmWindowManager *>(bmain->wm.first);

  LISTBASE_FOREACH (wmWindow *, window, &wm->windows) {
    Scene *scene = WM_window_get_active_scene(window);
    if ((scene->audio.flag & AUDIO_SYNC) == 0) {
      continue;
    }
    ViewLayer *view_layer = WM_window_get_active_view_layer(window);
    Depsgraph *depsgraph = BKE_scene_get_depsgraph(scene, view_layer);
    if (depsgraph == nullptr) {
      continue;
    }
    BKE_sound_lock();
    Scene *scene_eval = DEG_get_evaluated_scene(depsgraph);
    BKE_sound_jack_scene_update(scene_eval, mode, time);
    BKE_sound_unlock();
  }
}

void WM_init(bContext *C, int argc, const char **argv)
{

  if (!G.background) {
    wm_ghost_init(C); /* NOTE: it assigns C to ghost! */
    wm_init_cursor_data();
    BKE_sound_jack_sync_callback_set(sound_jack_sync_callback);
  }

  GHOST_CreateSystemPaths();

  BKE_addon_pref_type_init();
  BKE_keyconfig_pref_type_init();

  wm_operatortype_init();
  wm_operatortypes_register();

  WM_paneltype_init(); /* Lookup table only. */
  WM_menutype_init();
  WM_uilisttype_init();
  wm_gizmotype_init();
  wm_gizmogrouptype_init();

  ED_undosys_type_init();

  BKE_library_callback_free_notifier_reference_set(WM_main_remove_notifier_reference);
  BKE_region_callback_free_gizmomap_set(wm_gizmomap_remove);
  BKE_region_callback_refresh_tag_gizmomap_set(WM_gizmomap_tag_refresh);
  BKE_library_callback_remap_editor_id_reference_set(WM_main_remap_editor_id_reference);
  BKE_spacedata_callback_id_remap_set(ED_spacedata_id_remap_single);
  DEG_editors_set_update_cb(ED_render_id_flush_update, ED_render_scene_update);

  ED_spacetypes_init();

  ED_node_init_butfuncs();

  BLF_init(); /* Please update source/gamengine/GamePlayer/GPG_ghost.cpp if you change this */

  BLT_lang_init();
  /* Must call first before doing any `.blend` file reading,
   * since versioning code may create new IDs. See #57066. */
  BLT_lang_set(nullptr);

  /* Init icons before reading .blend files for preview icons, which can
   * get triggered by the depsgraph. This is also done in background mode
   * for scripts that do background processing with preview icons. */
  BKE_icons_init(BIFICONID_LAST);

  /* Reports can't be initialized before the window-manager,
   * but keep before file reading, since that may report errors */
  wm_init_reports(C);

  WM_msgbus_types_init();

  /* Studio-lights needs to be init before we read the home-file,
   * otherwise the versioning cannot find the default studio-light. */
  BKE_studiolight_init();

  BLI_assert((G.fileflags & G_FILE_NO_UI) == 0);

  /**
   * NOTE(@ideasman42): Startup file and order of initialization.
   *
   * Loading #BLENDER_STARTUP_FILE, #BLENDER_USERPREF_FILE, starting Python and other sub-systems,
   * have inter-dependencies, for example.
   *
   * - Some sub-systems depend on the preferences (initializing icons depend on the theme).
   * - Add-ons depends on the preferences to know what has been enabled.
   * - Add-ons depends on the window-manger to register their key-maps.
   * - Evaluating the startup file depends on Python for animation-drivers (see #89046).
   * - Starting Python depends on the startup file so key-maps can be added in the window-manger.
   *
   * Loading preferences early, then application subsystems and finally the startup data would
   * simplify things if it weren't for key-maps being part of the window-manager
   * which is blend file data.
   * Creating a dummy window-manager early, or moving the key-maps into the preferences
   * would resolve this and may be worth looking into long-term, see: D12184 for details.
   */
  struct wmFileReadPost_Params *params_file_read_post = nullptr;
  wmHomeFileRead_Params read_homefile_params{};
  read_homefile_params.use_data = true;
  read_homefile_params.use_userdef = true;
  read_homefile_params.use_factory_settings = G.factory_startup;
  read_homefile_params.use_empty_data = false;
  read_homefile_params.filepath_startup_override = nullptr;
  read_homefile_params.app_template_override = WM_init_state_app_template_get();

  wm_homefile_read_ex(C, &read_homefile_params, nullptr, &params_file_read_post);

  /* NOTE: leave `G_MAIN->filepath` set to an empty string since this
   * matches behavior after loading a new file. */
  BLI_assert(G_MAIN->filepath[0] == '\0');

  /* Call again to set from preferences. */
  BLT_lang_set(nullptr);

  /* For file-system. Called here so can include user preference paths if needed. */
  ED_file_init();

  if (!G.background) {
    GPU_render_begin();

#ifdef WITH_INPUT_NDOF
    /* Sets 3D mouse dead-zone. */
    WM_ndof_deadzone_set(U.ndof_deadzone);
#endif
    WM_init_opengl();

    if (!WM_platform_support_perform_checks()) {
      /* No attempt to avoid memory leaks here. */
      exit(-1);
    }

    GPU_context_begin_frame(GPU_context_active_get());
    UI_init();
    GPU_context_end_frame(GPU_context_active_get());
    GPU_render_end();
  }

  BKE_subdiv_init();

  ED_spacemacros_init();

#ifdef WITH_PYTHON
  BPY_python_start(C, argc, argv);
  BPY_python_reset(C);
#else
  UNUSED_VARS(argc, argv);
#endif

  if (!G.background) {
    if (wm_start_with_console) {
      GHOST_setConsoleWindowState(GHOST_kConsoleWindowStateShow);
    }
    else {
      GHOST_setConsoleWindowState(GHOST_kConsoleWindowStateHideForNonConsoleLaunch);
    }
  }

  ED_render_clear_mtex_copybuf();

  wm_history_file_read();

  STRNCPY(G.lib, BKE_main_blendfile_path_from_global());

  wm_homefile_read_post(C, params_file_read_post);
}

static bool wm_init_splash_show_on_startup_check()
{
  if (U.uiflag & USER_SPLASH_DISABLE) {
    return false;
  }

  bool use_splash = false;

  const char *blendfile_path = BKE_main_blendfile_path_from_global();
  if (blendfile_path[0] == '\0') {
    /* Common case, no file is loaded, show the splash. */
    use_splash = true;
  }
  else {
    /* A less common case, if there is no user preferences, show the splash screen
     * so the user has the opportunity to restore settings from a previous version. */
    const char *const cfgdir = BKE_appdir_folder_id(BLENDER_USER_CONFIG, nullptr);
    if (cfgdir) {
      char userpref[FILE_MAX];
      BLI_path_join(userpref, sizeof(userpref), cfgdir, BLENDER_USERPREF_FILE);
      if (!BLI_exists(userpref)) {
        use_splash = true;
      }
    }
    else {
      use_splash = true;
    }
  }

  return use_splash;
}

void WM_init_splash_on_startup(bContext *C)
{
  if (!wm_init_splash_show_on_startup_check()) {
    return;
  }

  WM_init_splash(C);
}

void WM_init_splash(bContext *C)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  /* NOTE(@ideasman42): this should practically never happen. */
  if (UNLIKELY(BLI_listbase_is_empty(&wm->windows))) {
    return;
  }

  wmWindow *prevwin = CTX_wm_window(C);
  CTX_wm_window_set(C, static_cast<wmWindow *>(wm->windows.first));
  WM_operator_name_call(C, "WM_OT_splash", WM_OP_INVOKE_DEFAULT, nullptr, nullptr);
  CTX_wm_window_set(C, prevwin);
}

bool WM_init_game(bContext *C)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  wmWindow *win;

  ScrArea *sa;
  ARegion *ar = NULL;

  Scene *scene = CTX_data_scene(C);

  if (!scene) {
    /* XXX, this should not be needed. */
    Main *bmain = CTX_data_main(C);
    scene = (Scene *)bmain->scenes.first;
  }

  win = (wmWindow *)wm->windows.first;

  /* first to get a valid window */
  if (win)
    CTX_wm_window_set(C, win);

  sa = BKE_screen_find_big_area(CTX_wm_screen(C), SPACE_VIEW3D, 0);
  ar = BKE_area_find_region_type(sa, RGN_TYPE_WINDOW);

  /* if we have a valid 3D view */
  if (sa && ar) {
    ARegion *arhide;

    CTX_wm_area_set(C, sa);
    CTX_wm_region_set(C, ar);

    /* disable quad view */
    if (ar->alignment == RGN_ALIGN_QSPLIT)
      WM_operator_name_call(C, "SCREEN_OT_region_quadview", WM_OP_EXEC_DEFAULT, NULL, NULL);

    /* toolbox, properties panel and header are hidden */
    for (arhide = (ARegion *)sa->regionbase.first; arhide; arhide = arhide->next) {
      if (arhide->regiontype != RGN_TYPE_WINDOW) {
        if (!(arhide->flag & RGN_FLAG_HIDDEN)) {
          ED_region_toggle_hidden(C, arhide);
        }
      }
    }

    /* full screen the area */
    if (!sa->full) {
      ED_screen_state_toggle(C, win, sa, SCREENMAXIMIZED);
    }

    /* Fullscreen */
    if ((scene->gm.playerflag & GAME_PLAYER_FULLSCREEN)) {
      WM_operator_name_call(C, "WM_OT_window_fullscreen_toggle", WM_OP_EXEC_DEFAULT, NULL, NULL);
      wm_get_screensize(&ar->winrct.xmax, &ar->winrct.ymax);
      ar->winx = ar->winrct.xmax + 1;
      ar->winy = ar->winrct.ymax + 1;
    }
    else {
      GHOST_RectangleHandle rect = GHOST_GetClientBounds(GHOST_WindowHandle(win->ghostwin));
      ar->winrct.ymax = GHOST_GetHeightRectangle(rect);
      ar->winrct.xmax = GHOST_GetWidthRectangle(rect);
      ar->winx = ar->winrct.xmax + 1;
      ar->winy = ar->winrct.ymax + 1;
      GHOST_DisposeRectangle(rect);
    }

    WM_operator_name_call(C, "VIEW3D_OT_game_start", WM_OP_EXEC_DEFAULT, NULL, NULL);

    BKE_sound_exit();

    return true;
  }
  else {
    ReportTimerInfo *rti;

    BKE_report(&wm->reports, RPT_ERROR, "No valid 3D View found, game auto start is not possible");

    /* After adding the report to the global list, reset the report timer. */
    WM_event_remove_timer(wm, NULL, wm->reports.reporttimer);

    /* Records time since last report was added */
    wm->reports.reporttimer = WM_event_add_timer(wm, CTX_wm_window(C), TIMER, 0.02);

    rti = (ReportTimerInfo *)MEM_callocN(sizeof(ReportTimerInfo), "ReportTimerInfo");
    wm->reports.reporttimer->customdata = rti;

    return false;
  }
}

/* free strings of open recent files */
static void free_openrecent(void)
{
  LISTBASE_FOREACH (RecentFile *, recent, &G.recent_files) {
    MEM_freeN(recent->filepath);
  }

  BLI_freelistN(&(G.recent_files));
}

#ifdef WIN32
/* Read console events until there is a key event. Also returns on any error. */
static void wait_for_console_key(void)
{
  HANDLE hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);

  if (!ELEM(hConsoleInput, nullptr, INVALID_HANDLE_VALUE) &&
      FlushConsoleInputBuffer(hConsoleInput)) {
    for (;;) {
      INPUT_RECORD buffer;
      DWORD ignored;

      if (!ReadConsoleInput(hConsoleInput, &buffer, 1, &ignored)) {
        break;
      }

      if (buffer.EventType == KEY_EVENT) {
        break;
      }
    }
  }
}
#endif

static int wm_exit_handler(bContext *C, const wmEvent *event, void *userdata)
{
  WM_exit(C, EXIT_SUCCESS);

  UNUSED_VARS(event, userdata);
  return WM_UI_HANDLER_BREAK;
}

void wm_exit_schedule_delayed(const bContext *C)
{
  /* What we do here is a little bit hacky, but quite simple and doesn't require bigger
   * changes: Add a handler wrapping WM_exit() to cause a delayed call of it. */

  wmWindow *win = CTX_wm_window(C);

  /* Use modal UI handler for now.
   * Could add separate WM handlers or so, but probably not worth it. */
  WM_event_add_ui_handler(
      C, &win->modalhandlers, wm_exit_handler, nullptr, nullptr, eWM_EventHandlerFlag(0));
  WM_event_add_mousemove(win); /* ensure handler actually gets called */
}

void UV_clipboard_free();

void WM_exit_ex(bContext *C, const bool do_python, const bool do_user_exit_actions)
{
  wmWindowManager *wm = C ? CTX_wm_manager(C) : nullptr;

  /* While nothing technically prevents saving user data in background mode,
   * don't do this as not typically useful and more likely to cause problems
   * if automated scripts happen to write changes to the preferences for e.g.
   * Saving #BLENDER_QUIT_FILE is also not likely to be desired either. */
  BLI_assert(G.background ? (do_user_exit_actions == false) : true);

  /* first wrap up running stuff, we assume only the active WM is running */
  /* modal handlers are on window level freed, others too? */
  /* NOTE: same code copied in `wm_files.cc`. */
  if (C && wm) {
    if (do_user_exit_actions) {
      struct MemFile *undo_memfile = wm->undo_stack ?
                                         ED_undosys_stack_memfile_get_active(wm->undo_stack) :
                                         nullptr;
      if (undo_memfile != nullptr) {
        /* save the undo state as quit.blend */
        Main *bmain = CTX_data_main(C);
        char filepath[FILE_MAX];
        bool has_edited;
        const int fileflags = G.fileflags & ~(G_FILE_COMPRESS | G_FILE_AUTOPLAY);

        BLI_path_join(filepath, sizeof(filepath), BKE_tempdir_base(), BLENDER_QUIT_FILE);

        has_edited = ED_editors_flush_edits(bmain);

        BlendFileWriteParams blend_file_write_params{};
        if ((has_edited &&
             BLO_write_file(bmain, filepath, fileflags, &blend_file_write_params, nullptr)) ||
            BLO_memfile_write_file(undo_memfile, filepath))
        {
          printf("Saved session recovery to \"%s\"\n", filepath);
        }
      }
    }

    WM_jobs_kill_all(wm);

    LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
      CTX_wm_window_set(C, win); /* needed by operator close callbacks */
      WM_event_remove_handlers(C, &win->handlers);
      WM_event_remove_handlers(C, &win->modalhandlers);
      ED_screen_exit(C, win, WM_window_get_active_screen(win));
    }

    if (do_user_exit_actions) {
      if ((U.pref_flag & USER_PREF_FLAG_SAVE) && ((G.f & G_FLAG_USERPREF_NO_SAVE_ON_EXIT) == 0)) {
        if (U.runtime.is_dirty) {
          BKE_blendfile_userdef_write_all(nullptr);
        }
      }
      /* Free the callback data used on file-open
       * (will be set when a recover operation has run). */
      wm_test_autorun_revert_action_set(nullptr, nullptr);
    }
  }

#if defined(WITH_PYTHON) && !defined(WITH_PYTHON_MODULE)
  /* Without this, we there isn't a good way to manage false-positive resource leaks
   * where a #PyObject references memory allocated with guarded-alloc, #71362.
   *
   * This allows add-ons to free resources when unregistered (which is good practice anyway).
   *
   * Don't run this code when built as a Python module as this runs when Python is in the
   * process of shutting down, where running a snippet like this will crash, see #82675.
   * Instead use the `atexit` module, installed by #BPY_python_start */
  const char *imports[2] = {"addon_utils", nullptr};
  BPY_run_string_eval(C, imports, "addon_utils.disable_all()");
#endif

  BLI_timer_free();

  WM_paneltype_clear();

  BKE_addon_pref_type_free();
  BKE_keyconfig_pref_type_free();
  BKE_materials_exit();

  wm_operatortype_free();
  wm_surfaces_free();
  wm_dropbox_free();
  WM_menutype_free();

  /* all non-screen and non-space stuff editors did, like editmode */
  if (C) {
    Main *bmain = CTX_data_main(C);
    ED_editors_exit(bmain, true);
  }

  ED_undosys_type_free();

  free_openrecent();

  BKE_mball_cubeTable_free();

  /* render code might still access databases */
  RE_FreeAllRender();
  RE_engines_exit();

  ED_preview_free_dbase(); /* frees a Main dbase, before BKE_blender_free! */
  ED_preview_restart_queue_free();
  ED_assetlist_storage_exit();

  if (wm) {
    /* Before BKE_blender_free! - since the ListBases get freed there. */
    wm_free_reports(wm);
  }

  SEQ_clipboard_free(); /* sequencer.c */
  BKE_tracking_clipboard_free();
  BKE_mask_clipboard_free();
  BKE_vfont_clipboard_free();
  ED_node_clipboard_free();
  UV_clipboard_free();

#ifdef WITH_COMPOSITOR_CPU
  COM_deinitialize();
#endif

  BKE_subdiv_exit();

  if (opengl_is_init) {
    BKE_image_free_unused_gpu_textures();
  }

  BKE_blender_free(); /* blender.c, does entire library and spacetypes */

  /* Free the GPU subdivision data after the database to ensure that subdivision structs used by
   * the modifiers were garbage collected. */
  if (opengl_is_init) {
    DRW_subdiv_free();
  }

  ANIM_fcurves_copybuf_free();
  ANIM_drivers_copybuf_free();
  ANIM_driver_vars_copybuf_free();
  ANIM_fmodifiers_copybuf_free();
  ED_gpencil_anim_copybuf_free();
  ED_gpencil_strokes_copybuf_free();

  /* free gizmo-maps after freeing blender,
   * so no deleted data get accessed during cleaning up of areas. */
  wm_gizmomaptypes_free();
  wm_gizmogrouptype_free();
  wm_gizmotype_free();
  /* Same for UI-list types. */
  WM_uilisttype_free();

  BLF_exit();

  BLT_lang_free();

  ANIM_keyingset_infos_exit();

  //  free_txt_data();

#ifdef WITH_PYTHON
  /* option not to close python so we can use 'atexit' */
  if (do_python && ((C == nullptr) || CTX_py_init_get(C))) {
    /* NOTE: (old note)
     * before BKE_blender_free so Python's garbage-collection happens while library still exists.
     * Needed at least for a rare crash that can happen in python-drivers.
     *
     * Update for Blender 2.5, move after #BKE_blender_free because Blender now holds references
     * to #PyObject's so #Py_DECREF'ing them after Python ends causes bad problems every time
     * the python-driver bug can be fixed if it happens again we can deal with it then. */
    BPY_python_end();
  }
#else
  (void)do_python;
#endif

  ED_file_exit(); /* For file-selector menu data. */

  /* Delete GPU resources and context. The UI also uses GPU resources and so
   * is also deleted with the context active. */
  if (opengl_is_init) {
    DRW_opengl_context_enable_ex(false);
    UI_exit();
    GPU_pass_cache_free();
    GPU_exit();
    DRW_opengl_context_disable_ex(false);
    DRW_opengl_context_destroy();
  }
  else {
    UI_exit();
  }

  BKE_blender_userdef_data_free(&U, false);

  RNA_exit(); /* should be after BPY_python_end so struct python slots are cleared */

  wm_ghost_exit();

  CTX_free(C);
#ifdef WITH_GAMEENGINE
  SYS_DeleteSystem(SYS_GetSystem());
#endif

  GHOST_DisposeSystemPaths();

  DNA_sdna_current_free();

  BLI_threadapi_exit();
  BLI_task_scheduler_exit();

  /* No need to call this early, rather do it late so that other
   * pieces of Blender using sound may exit cleanly, see also #50676. */
  BKE_sound_exit();

  BKE_appdir_exit();

  BKE_blender_atexit();

  wm_autosave_delete();

  BKE_tempdir_session_purge();

  /* Logging cannot be called after exiting (#CLOG_INFO, #CLOG_WARN etc will crash).
   * So postpone exiting until other sub-systems that may use logging have shut down. */
  CLG_exit();
}

void WM_exit(bContext *C, const int exit_code)
{
  const bool do_user_exit_actions = G.background ? false : (exit_code == EXIT_SUCCESS);
  WM_exit_ex(C, true, do_user_exit_actions);

  printf("\nBlender quit\n");

#ifdef WIN32
  /* ask user to press a key when in debug mode */
  if (G.debug & G_DEBUG) {
    printf("Press any key to exit . . .\n\n");
    wait_for_console_key();
  }
#endif

  exit(exit_code);
}

void WM_script_tag_reload(void)
{
  UI_interface_tag_script_reload();
}

/* UPBGE */
void WM_init_opengl_blenderplayer(void *ghost_system)
{
  /* must be called only once */
  BLI_assert(opengl_is_init == false);
  /* Ghost is still not init elsewhere in background mode. */
  ////////// wm_ghost_init(NULL);

  /* NEEDS TO HAVE AN OGL CONTEXT BOUND FIRST!!!!!!!!!!!!!!!!!!! */
  DRW_opengl_context_create_blenderplayer(ghost_system);
  GPU_init();
  GPU_pass_cache_init();
#ifdef WITH_OPENSUBDIV
  BKE_subsurf_osd_init();
#endif
  opengl_is_init = true;
}
/* End of UPBGE */
