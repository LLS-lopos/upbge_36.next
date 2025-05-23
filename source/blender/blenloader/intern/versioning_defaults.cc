/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup blenloader
 *
 * This file handles updating the `startup.blend`, this is used when reading old files.
 *
 * Unlike regular versioning this makes changes that ensure the startup file
 * has brushes and other presets setup to take advantage of newer features.
 *
 * To update preference defaults see `userdef_default.c`.
 */

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_math_vector_types.hh"
#include "BLI_string.h"
#include "BLI_system.h"
#include "BLI_utildefines.h"

#include "DNA_camera_types.h"
#include "DNA_curveprofile_types.h"
#include "DNA_gpencil_legacy_types.h"
#include "DNA_light_types.h"
#include "DNA_mask_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_workspace_types.h"
#include "DNA_world_types.h"  // UPBGE

#include "BKE_appdir.h"
#include "BKE_attribute.hh"
#include "BKE_brush.h"
#include "BKE_colortools.h"
#include "BKE_curveprofile.h"
#include "BKE_customdata.h"
#include "BKE_gpencil_legacy.h"
#include "BKE_idprop.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_main_namemap.h"
#include "BKE_material.h"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BKE_node_tree_update.h"
#include "BKE_paint.h"
#include "BKE_screen.h"
#include "BKE_workspace.h"

#include "BLO_readfile.h"

#include "BLT_translation.h"

#include "versioning_common.h"

/* Make preferences read-only, use versioning_userdef.c. */
#define U (*((const UserDef *)&U))

static bool blo_is_builtin_template(const char *app_template)
{
  /* For all builtin templates shipped with Blender. */
  return (
      !app_template ||
      STR_ELEM(app_template, N_("2D_Animation"), N_("Sculpting"), N_("VFX"), N_("Video_Editing")));
}

static void blo_update_defaults_screen(bScreen *screen,
                                       const char *app_template,
                                       const char *workspace_name)
{
  /* For all app templates. */
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      /* Some toolbars have been saved as initialized,
       * we don't want them to have odd zoom-level or scrolling set, see: #47047 */
      if (ELEM(region->regiontype, RGN_TYPE_UI, RGN_TYPE_TOOLS, RGN_TYPE_TOOL_PROPS)) {
        region->v2d.flag &= ~V2D_IS_INIT;
      }
    }

    /* Set default folder. */
    LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
      if (sl->spacetype == SPACE_FILE) {
        SpaceFile *sfile = (SpaceFile *)sl;
        if (sfile->params) {
          const char *dir_default = BKE_appdir_folder_default();
          if (dir_default) {
            STRNCPY(sfile->params->dir, dir_default);
            sfile->params->file[0] = '\0';
          }
        }
      }
    }
  }

  /* For builtin templates only. */
  if (!blo_is_builtin_template(app_template)) {
    return;
  }

  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      /* Remove all stored panels, we want to use defaults
       * (order, open/closed) as defined by UI code here! */
      BKE_area_region_panels_free(&region->panels);
      BLI_freelistN(&region->panels_category_active);

      /* Reset size so it uses consistent defaults from the region types. */
      region->sizex = 0;
      region->sizey = 0;
    }

    if (area->spacetype == SPACE_IMAGE) {
      if (STREQ(workspace_name, "UV Editing")) {
        SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);
        if (sima->mode == SI_MODE_VIEW) {
          sima->mode = SI_MODE_UV;
        }
      }
    }
    else if (area->spacetype == SPACE_ACTION) {
      /* Show markers region, hide channels and collapse summary in timelines. */
      SpaceAction *saction = static_cast<SpaceAction *>(area->spacedata.first);
      saction->flag |= SACTION_SHOW_MARKERS;
      if (saction->mode == SACTCONT_TIMELINE) {
        saction->ads.flag |= ADS_FLAG_SUMMARY_COLLAPSED;

        LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
          if (region->regiontype == RGN_TYPE_CHANNELS) {
            region->flag |= RGN_FLAG_HIDDEN;
          }
        }
      }
      else {
        /* Open properties panel by default. */
        LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
          if (region->regiontype == RGN_TYPE_UI) {
            region->flag &= ~RGN_FLAG_HIDDEN;
          }
        }
      }
    }
    else if (area->spacetype == SPACE_GRAPH) {
      SpaceGraph *sipo = static_cast<SpaceGraph *>(area->spacedata.first);
      sipo->flag |= SIPO_SHOW_MARKERS;
    }
    else if (area->spacetype == SPACE_NLA) {
      SpaceNla *snla = static_cast<SpaceNla *>(area->spacedata.first);
      snla->flag |= SNLA_SHOW_MARKERS;
    }
    else if (area->spacetype == SPACE_SEQ) {
      SpaceSeq *seq = static_cast<SpaceSeq *>(area->spacedata.first);
      seq->flag |= SEQ_SHOW_MARKERS | SEQ_ZOOM_TO_FIT | SEQ_USE_PROXIES | SEQ_SHOW_OVERLAY;
      seq->render_size = SEQ_RENDER_SIZE_PROXY_100;
      seq->timeline_overlay.flag |= SEQ_TIMELINE_SHOW_STRIP_SOURCE | SEQ_TIMELINE_SHOW_STRIP_NAME |
                                    SEQ_TIMELINE_SHOW_STRIP_DURATION | SEQ_TIMELINE_SHOW_GRID |
                                    SEQ_TIMELINE_SHOW_STRIP_COLOR_TAG;
      seq->preview_overlay.flag |= SEQ_PREVIEW_SHOW_OUTLINE_SELECTED;
    }
    else if (area->spacetype == SPACE_TEXT) {
      /* Show syntax and line numbers in Script workspace text editor. */
      SpaceText *stext = static_cast<SpaceText *>(area->spacedata.first);
      stext->showsyntax = true;
      stext->showlinenrs = true;
    }
    else if (area->spacetype == SPACE_VIEW3D) {
      View3D *v3d = static_cast<View3D *>(area->spacedata.first);
      /* Screen space cavity by default for faster performance. */
      v3d->shading.cavity_type = V3D_SHADING_CAVITY_CURVATURE;
      v3d->shading.flag |= V3D_SHADING_SPECULAR_HIGHLIGHT;
      v3d->overlay.texture_paint_mode_opacity = 1.0f;
      v3d->overlay.weight_paint_mode_opacity = 1.0f;
      v3d->overlay.vertex_paint_mode_opacity = 1.0f;
      /* Use dimmed selected edges. */
      v3d->overlay.edit_flag &= ~V3D_OVERLAY_EDIT_EDGES;
      /* grease pencil settings */
      v3d->vertex_opacity = 1.0f;
      v3d->gp_flag |= V3D_GP_SHOW_EDIT_LINES;
      /* Remove dither pattern in wireframe mode. */
      v3d->shading.xray_alpha_wire = 0.0f;
      v3d->clip_start = 0.01f;
      /* Skip startups that use the viewport color by default. */
      if (v3d->shading.background_type != V3D_SHADING_BACKGROUND_VIEWPORT) {
        copy_v3_fl(v3d->shading.background_color, 0.05f);
      }
      /* Disable Curve Normals. */
      v3d->overlay.edit_flag &= ~V3D_OVERLAY_EDIT_CU_NORMALS;
      v3d->overlay.normals_constant_screen_size = 7.0f;
    }
    else if (area->spacetype == SPACE_CLIP) {
      SpaceClip *sclip = static_cast<SpaceClip *>(area->spacedata.first);
      sclip->around = V3D_AROUND_CENTER_MEDIAN;
      sclip->mask_info.blend_factor = 0.7f;
      sclip->mask_info.draw_flag = MASK_DRAWFLAG_SPLINE;
    }
  }

  /* Show tool-header by default (for most cases at least, hide for others). */
  const bool hide_image_tool_header = STREQ(workspace_name, "Rendering");
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
      ListBase *regionbase = (sl == static_cast<SpaceLink *>(area->spacedata.first)) ?
                                 &area->regionbase :
                                 &sl->regionbase;

      LISTBASE_FOREACH (ARegion *, region, regionbase) {
        if (region->regiontype == RGN_TYPE_TOOL_HEADER) {
          if (((sl->spacetype == SPACE_IMAGE) && hide_image_tool_header) ||
              sl->spacetype == SPACE_SEQ) {
            region->flag |= RGN_FLAG_HIDDEN;
          }
          else {
            region->flag &= ~(RGN_FLAG_HIDDEN | RGN_FLAG_HIDDEN_BY_USER);
          }
        }
      }
    }
  }

  /* 2D animation template. */
  if (app_template && STREQ(app_template, "2D_Animation")) {
    LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
      if (area->spacetype == SPACE_ACTION) {
        SpaceAction *saction = static_cast<SpaceAction *>(area->spacedata.first);
        /* Enable Sliders. */
        saction->flag |= SACTION_SLIDERS;
      }
      else if (area->spacetype == SPACE_VIEW3D) {
        View3D *v3d = static_cast<View3D *>(area->spacedata.first);
        /* Set Material Color by default. */
        v3d->shading.color_type = V3D_SHADING_MATERIAL_COLOR;
        /* Enable Annotations. */
        v3d->flag2 |= V3D_SHOW_ANNOTATION;
      }
    }
  }
}

void BLO_update_defaults_workspace(WorkSpace *workspace, const char *app_template)
{
  LISTBASE_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
    if (layout->screen) {
      blo_update_defaults_screen(layout->screen, app_template, workspace->id.name + 2);
    }
  }

  if (blo_is_builtin_template(app_template)) {
    /* Clear all tools to use default options instead, ignore the tool saved in the file. */
    while (!BLI_listbase_is_empty(&workspace->tools)) {
      BKE_workspace_tool_remove(workspace, static_cast<bToolRef *>(workspace->tools.first));
    }

    /* For 2D animation template. */
    if (STREQ(workspace->id.name + 2, "Drawing")) {
      workspace->object_mode = OB_MODE_PAINT_GPENCIL;
    }

    /* For Sculpting template. */
    if (STREQ(workspace->id.name + 2, "Sculpting")) {
      LISTBASE_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
        bScreen *screen = layout->screen;
        if (screen) {
          LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
            LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
              if (area->spacetype == SPACE_VIEW3D) {
                View3D *v3d = static_cast<View3D *>(area->spacedata.first);
                v3d->shading.flag &= ~V3D_SHADING_CAVITY;
                copy_v3_fl(v3d->shading.single_color, 1.0f);
                STRNCPY(v3d->shading.matcap, "basic_1");
              }
            }
          }
        }
      }
    }
  }
}

static void blo_update_defaults_scene(Main *bmain, Scene *scene)
{
  STRNCPY(scene->r.engine, RE_engine_id_BLENDER_EEVEE);

  scene->r.cfra = 1.0f;

  /* Don't enable compositing nodes. */
  if (scene->nodetree) {
    ntreeFreeEmbeddedTree(scene->nodetree);
    MEM_freeN(scene->nodetree);
    scene->nodetree = nullptr;
    scene->use_nodes = false;
  }

  /* Rename render layers. */
  BKE_view_layer_rename(
      bmain, scene, static_cast<ViewLayer *>(scene->view_layers.first), "ViewLayer");

  /* Disable Z pass by default. */
  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    view_layer->passflag &= ~SCE_PASS_Z;
  }

  /* New EEVEE defaults. */
  scene->eevee.bloom_intensity = 0.05f;
  scene->eevee.bloom_clamp = 0.0f;
  scene->eevee.motion_blur_shutter = 0.5f;

  copy_v3_v3(scene->display.light_direction, blender::float3(M_SQRT1_3));
  copy_v2_fl2(scene->safe_areas.title, 0.1f, 0.05f);
  copy_v2_fl2(scene->safe_areas.action, 0.035f, 0.035f);

  /* Change default cube-map quality. */
  scene->eevee.gi_filter_quality = 3.0f;

  /* Enable Soft Shadows by default. */
  scene->eevee.flag |= SCE_EEVEE_SHADOW_SOFT;

  /* Be sure `curfalloff` and primitive are initialized. */
  ToolSettings *ts = scene->toolsettings;
  if (ts->gp_sculpt.cur_falloff == nullptr) {
    ts->gp_sculpt.cur_falloff = BKE_curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
    CurveMapping *gp_falloff_curve = ts->gp_sculpt.cur_falloff;
    BKE_curvemapping_init(gp_falloff_curve);
    BKE_curvemap_reset(gp_falloff_curve->cm,
                       &gp_falloff_curve->clipr,
                       CURVE_PRESET_GAUSS,
                       CURVEMAP_SLOPE_POSITIVE);
  }
  if (ts->gp_sculpt.cur_primitive == nullptr) {
    ts->gp_sculpt.cur_primitive = BKE_curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
    CurveMapping *gp_primitive_curve = ts->gp_sculpt.cur_primitive;
    BKE_curvemapping_init(gp_primitive_curve);
    BKE_curvemap_reset(gp_primitive_curve->cm,
                       &gp_primitive_curve->clipr,
                       CURVE_PRESET_BELL,
                       CURVEMAP_SLOPE_POSITIVE);
  }

  if (ts->sculpt) {
    ts->sculpt->paint.symmetry_flags |= PAINT_SYMMETRY_FEATHER;
  }

  /* Correct default startup UVs. */
  Mesh *me = static_cast<Mesh *>(BLI_findstring(&bmain->meshes, "Cube", offsetof(ID, name) + 2));
  if (me && (me->totloop == 24) && CustomData_has_layer(&me->ldata, CD_PROP_FLOAT2)) {
    const float uv_values[24][2] = {
        {0.625, 0.50}, {0.875, 0.50}, {0.875, 0.75}, {0.625, 0.75}, {0.375, 0.75}, {0.625, 0.75},
        {0.625, 1.00}, {0.375, 1.00}, {0.375, 0.00}, {0.625, 0.00}, {0.625, 0.25}, {0.375, 0.25},
        {0.125, 0.50}, {0.375, 0.50}, {0.375, 0.75}, {0.125, 0.75}, {0.375, 0.50}, {0.625, 0.50},
        {0.625, 0.75}, {0.375, 0.75}, {0.375, 0.25}, {0.625, 0.25}, {0.625, 0.50}, {0.375, 0.50},
    };
    float(*mloopuv)[2] = static_cast<float(*)[2]>(
        CustomData_get_layer_for_write(&me->ldata, CD_PROP_FLOAT2, me->totloop));
    memcpy(mloopuv, uv_values, sizeof(float[2]) * me->totloop);
  }

  /* Make sure that the curve profile is initialized */
  if (ts->custom_bevel_profile_preset == nullptr) {
    ts->custom_bevel_profile_preset = BKE_curveprofile_add(PROF_PRESET_LINE);
  }

  /* Clear ID properties so Cycles gets defaults. */
  IDProperty *idprop = IDP_GetProperties(&scene->id, false);
  if (idprop) {
    IDP_ClearProperty(idprop);
  }
}

void BLO_update_defaults_startup_blend(Main *bmain, const char *app_template)
{
  /*********************UPBGE*********************/
  // WARNING: ALWAYS KEEP THIS IN BLO_update_defaults_startup_blend
  for (Scene *sce = static_cast<Scene *>(bmain->scenes.first); sce; sce = static_cast<Scene *>(sce->id.next)) {
    /* game data */
    sce->gm.stereoflag = STEREO_NOSTEREO;
    sce->gm.stereomode = STEREO_ANAGLYPH;
    sce->gm.eyeseparation = 0.10;
    sce->gm.xplay = 1280;
    sce->gm.yplay = 720;
    sce->gm.samples_per_frame = 1;
    sce->gm.freqplay = 60;
    sce->gm.depth = 32;
    sce->gm.gravity = 9.8f;
    sce->gm.physicsEngine = WOPHY_BULLET;
    sce->gm.mode = WO_ACTIVITY_CULLING;
    sce->gm.occlusionRes = 128;
    sce->gm.ticrate = 60;
    sce->gm.maxlogicstep = 10;
    sce->gm.physubstep = 10;
    sce->gm.maxphystep = 10;
    sce->gm.lineardeactthreshold = 0.8f;
    sce->gm.angulardeactthreshold = 1.0f;
    sce->gm.deactivationtime = 2.0f;

    sce->gm.obstacleSimulation = OBSTSIMULATION_NONE;
    sce->gm.levelHeight = 2.f;

    sce->gm.recastData.cellsize = 0.3f;
    sce->gm.recastData.cellheight = 0.2f;
    sce->gm.recastData.agentmaxslope = M_PI_4;
    sce->gm.recastData.agentmaxclimb = 0.9f;
    sce->gm.recastData.agentheight = 2.0f;
    sce->gm.recastData.agentradius = 0.6f;
    sce->gm.recastData.edgemaxlen = 12.0f;
    sce->gm.recastData.edgemaxerror = 1.3f;
    sce->gm.recastData.regionminsize = 8.f;
    sce->gm.recastData.regionmergesize = 20.f;
    sce->gm.recastData.vertsperpoly = 6;
    sce->gm.recastData.detailsampledist = 6.0f;
    sce->gm.recastData.detailsamplemaxerror = 1.0f;
    sce->gm.recastData.partitioning = RC_PARTITION_WATERSHED;

    sce->gm.exitkey = 218;  // Blender key code for ESC

    sce->gm.flag |= GAME_USE_UNDO;

    sce->gm.lodflag = SCE_LOD_USE_HYST;
    sce->gm.scehysteresis = 10;
  }
  for (Object *ob = static_cast<Object *>(bmain->objects.first); ob; ob = static_cast<Object *>(ob->id.next)) {
    /* UPBGE defaults*/
    ob->mass = ob->inertia = 1.0f;
    ob->formfactor = 0.4f;
    ob->damping = 0.04f;
    ob->rdamping = 0.1f;
    ob->anisotropicFriction[0] = 1.0f;
    ob->anisotropicFriction[1] = 1.0f;
    ob->anisotropicFriction[2] = 1.0f;
    ob->gameflag = OB_PROP | OB_COLLISION;
    ob->gameflag2 = 0;
    ob->margin = 0.04f;
    ob->friction = 0.5f;
    ob->init_state = 1;
    ob->state = 1;
    ob->obstacleRad = 1.0f;
    ob->step_height = 0.15f;
    ob->jump_speed = 10.0f;
    ob->fall_speed = 55.0f;
    ob->max_jumps = 1;
    ob->max_slope = M_PI_2;
    ob->col_group = 0x01;
    ob->col_mask = 0xffff;
    if (ob->type == OB_CAMERA) {
      Camera *cam = static_cast<Camera *>(ob->data);
      cam->gameflag |= GAME_CAM_OBJECT_ACTIVITY_CULLING;
    }
  }
  /***********************End of UPBGE**********************/

  /* For all app templates. */
  LISTBASE_FOREACH (WorkSpace *, workspace, &bmain->workspaces) {
    BLO_update_defaults_workspace(workspace, app_template);
  }

  /* New grease pencil brushes and vertex paint setup. */
  {
    /* Update Grease Pencil brushes. */
    Brush *brush;

    /* Pencil brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Pencil", "Pencil");

    /* Pen brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Pen", "Pen");

    /* Pen Soft brush. */
    brush = reinterpret_cast<Brush *>(
        do_versions_rename_id(bmain, ID_BR, "Draw Soft", "Pencil Soft"));
    if (brush) {
      brush->gpencil_settings->icon_id = GP_BRUSH_ICON_PEN;
    }

    /* Ink Pen brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Ink", "Ink Pen");

    /* Ink Pen Rough brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Noise", "Ink Pen Rough");

    /* Marker Bold brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Marker", "Marker Bold");

    /* Marker Chisel brush. */
    do_versions_rename_id(bmain, ID_BR, "Draw Block", "Marker Chisel");

    /* Remove useless Fill Area.001 brush. */
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, "Fill Area.001", offsetof(ID, name) + 2));
    if (brush) {
      BKE_id_delete(bmain, brush);
    }

    /* Rename and fix materials and enable default object lights on. */
    if (app_template && STREQ(app_template, "2D_Animation")) {
      Material *ma = nullptr;
      do_versions_rename_id(bmain, ID_MA, "Black", "Solid Stroke");
      do_versions_rename_id(bmain, ID_MA, "Red", "Squares Stroke");
      do_versions_rename_id(bmain, ID_MA, "Grey", "Solid Fill");
      do_versions_rename_id(bmain, ID_MA, "Black Dots", "Dots Stroke");

      /* Dots Stroke. */
      ma = static_cast<Material *>(
          BLI_findstring(&bmain->materials, "Dots Stroke", offsetof(ID, name) + 2));
      if (ma == nullptr) {
        ma = BKE_gpencil_material_add(bmain, "Dots Stroke");
      }
      ma->gp_style->mode = GP_MATERIAL_MODE_DOT;

      /* Squares Stroke. */
      ma = static_cast<Material *>(
          BLI_findstring(&bmain->materials, "Squares Stroke", offsetof(ID, name) + 2));
      if (ma == nullptr) {
        ma = BKE_gpencil_material_add(bmain, "Squares Stroke");
      }
      ma->gp_style->mode = GP_MATERIAL_MODE_SQUARE;

      /* Change Solid Stroke settings. */
      ma = static_cast<Material *>(
          BLI_findstring(&bmain->materials, "Solid Stroke", offsetof(ID, name) + 2));
      if (ma != nullptr) {
        ma->gp_style->mix_rgba[3] = 1.0f;
        ma->gp_style->texture_offset[0] = -0.5f;
        ma->gp_style->mix_factor = 0.5f;
      }

      /* Change Solid Fill settings. */
      ma = static_cast<Material *>(
          BLI_findstring(&bmain->materials, "Solid Fill", offsetof(ID, name) + 2));
      if (ma != nullptr) {
        ma->gp_style->flag &= ~GP_MATERIAL_STROKE_SHOW;
        ma->gp_style->mix_rgba[3] = 1.0f;
        ma->gp_style->texture_offset[0] = -0.5f;
        ma->gp_style->mix_factor = 0.5f;
      }

      Object *ob = static_cast<Object *>(
          BLI_findstring(&bmain->objects, "Stroke", offsetof(ID, name) + 2));
      if (ob && ob->type == OB_GPENCIL_LEGACY) {
        ob->dtx |= OB_USE_GPENCIL_LIGHTS;
      }
    }

    /* Reset all grease pencil brushes. */
    Scene *scene = static_cast<Scene *>(bmain->scenes.first);
    BKE_brush_gpencil_paint_presets(bmain, scene->toolsettings, true);
    BKE_brush_gpencil_sculpt_presets(bmain, scene->toolsettings, true);
    BKE_brush_gpencil_vertex_presets(bmain, scene->toolsettings, true);
    BKE_brush_gpencil_weight_presets(bmain, scene->toolsettings, true);

    /* Ensure new Paint modes. */
    BKE_paint_ensure_from_paintmode(scene, PAINT_MODE_VERTEX_GPENCIL);
    BKE_paint_ensure_from_paintmode(scene, PAINT_MODE_SCULPT_GPENCIL);
    BKE_paint_ensure_from_paintmode(scene, PAINT_MODE_WEIGHT_GPENCIL);

    /* Enable cursor. */
    GpPaint *gp_paint = scene->toolsettings->gp_paint;
    gp_paint->paint.flags |= PAINT_SHOW_BRUSH;

    /* Ensure Palette by default. */
    BKE_gpencil_palette_ensure(bmain, scene);
  }

  /* For builtin templates only. */
  if (!blo_is_builtin_template(app_template)) {
    return;
  }

  /* Work-spaces. */
  LISTBASE_FOREACH (wmWindowManager *, wm, &bmain->wm) {
    LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
      LISTBASE_FOREACH (WorkSpace *, workspace, &bmain->workspaces) {
        WorkSpaceLayout *layout = BKE_workspace_active_layout_for_workspace_get(
            win->workspace_hook, workspace);
        /* Name all screens by their workspaces (avoids 'Default.###' names). */
        /* Default only has one window. */
        if (layout->screen) {
          bScreen *screen = layout->screen;
          if (!STREQ(screen->id.name + 2, workspace->id.name + 2)) {
            BKE_main_namemap_remove_name(bmain, &screen->id, screen->id.name + 2);
            BLI_strncpy(screen->id.name + 2, workspace->id.name + 2, sizeof(screen->id.name) - 2);
            BLI_libblock_ensure_unique_name(bmain, screen->id.name);
          }
        }

        /* For some reason we have unused screens, needed until re-saving.
         * Clear unused layouts because they're visible in the outliner & Python API. */
        LISTBASE_FOREACH_MUTABLE (WorkSpaceLayout *, layout_iter, &workspace->layouts) {
          if (layout != layout_iter) {
            BKE_workspace_layout_remove(bmain, workspace, layout_iter);
          }
        }
      }
    }
  }

  /* Scenes */
  LISTBASE_FOREACH (Scene *, scene, &bmain->scenes) {
    blo_update_defaults_scene(bmain, scene);

    if (app_template && STREQ(app_template, "Video_Editing")) {
      /* Filmic is too slow, use standard until it is optimized. */
      STRNCPY(scene->view_settings.view_transform, "Standard");
      STRNCPY(scene->view_settings.look, "None");
    }
    else {
      /* AV Sync break physics sim caching, disable until that is fixed. */
      scene->audio.flag &= ~AUDIO_SYNC;
      scene->flag &= ~SCE_FRAME_DROP;
    }

    /* Change default selection mode for Grease Pencil. */
    if (app_template && STREQ(app_template, "2D_Animation")) {
      ToolSettings *ts = scene->toolsettings;
      ts->gpencil_selectmode_edit = GP_SELECTMODE_STROKE;
    }
  }

  /* Objects */
  do_versions_rename_id(bmain, ID_OB, "Lamp", "Light");
  do_versions_rename_id(bmain, ID_LA, "Lamp", "Light");

  if (app_template && STREQ(app_template, "2D_Animation")) {
    LISTBASE_FOREACH (Object *, object, &bmain->objects) {
      if (object->type == OB_GPENCIL_LEGACY) {
        /* Set grease pencil object in drawing mode */
        bGPdata *gpd = (bGPdata *)object->data;
        object->mode = OB_MODE_PAINT_GPENCIL;
        gpd->flag |= GP_DATA_STROKE_PAINTMODE;
        break;
      }
    }
  }

  LISTBASE_FOREACH (Mesh *, mesh, &bmain->meshes) {
    /* Match default for new meshes. */
    mesh->smoothresh = DEG2RADF(30);
    /* Match voxel remesher options for all existing meshes in templates. */
    mesh->flag |= ME_REMESH_REPROJECT_VOLUME | ME_REMESH_REPROJECT_PAINT_MASK |
                  ME_REMESH_REPROJECT_SCULPT_FACE_SETS | ME_REMESH_REPROJECT_VERTEX_COLORS;

    /* For Sculpting template. */
    if (app_template && STREQ(app_template, "Sculpting")) {
      mesh->remesh_voxel_size = 0.035f;
      BKE_mesh_smooth_flag_set(mesh, false);
    }
    else {
      /* Remove sculpt-mask data in default mesh objects for all non-sculpt templates. */
      CustomData_free_layers(&mesh->vdata, CD_PAINT_MASK, mesh->totvert);
      CustomData_free_layers(&mesh->ldata, CD_GRID_PAINT_MASK, mesh->totloop);
    }
    mesh->attributes_for_write().remove(".sculpt_face_set");
  }

  LISTBASE_FOREACH (Camera *, camera, &bmain->cameras) {
    /* Initialize to a useful value. */
    camera->dof.focus_distance = 10.0f;
    camera->dof.aperture_fstop = 2.8f;
  }

  LISTBASE_FOREACH (Light *, light, &bmain->lights) {
    /* Fix lights defaults. */
    light->clipsta = 0.05f;
    light->att_dist = 40.0f;
  }

  /* Materials */
  LISTBASE_FOREACH (Material *, ma, &bmain->materials) {
    /* Update default material to be a bit more rough. */
    ma->roughness = 0.5f;

    if (ma->nodetree) {
      for (bNode *node : ma->nodetree->all_nodes()) {
        if (node->type == SH_NODE_BSDF_PRINCIPLED) {
          bNodeSocket *roughness_socket = nodeFindSocket(node, SOCK_IN, "Roughness");
          bNodeSocketValueFloat *roughness_data = static_cast<bNodeSocketValueFloat *>(
              roughness_socket->default_value);
          roughness_data->value = 0.5f;
          node->custom2 = SHD_SUBSURFACE_RANDOM_WALK;
          BKE_ntree_update_tag_node_property(ma->nodetree, node);
        }
        else if (node->type == SH_NODE_SUBSURFACE_SCATTERING) {
          node->custom1 = SHD_SUBSURFACE_RANDOM_WALK;
          BKE_ntree_update_tag_node_property(ma->nodetree, node);
        }
      }
    }
  }

  /* Brushes */
  {
    /* Enable for UV sculpt (other brush types will be created as needed),
     * without this the grab brush will be active but not selectable from the list. */
    const char *brush_name = "Grab";
    Brush *brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (brush) {
      brush->ob_mode |= OB_MODE_EDIT;
    }
  }

  LISTBASE_FOREACH (Brush *, brush, &bmain->brushes) {
    brush->blur_kernel_radius = 2;

    /* Use full strength for all non-sculpt brushes,
     * when painting we want to use full color/weight always.
     *
     * Note that sculpt is an exception,
     * its values are overwritten by #BKE_brush_sculpt_reset below. */
    brush->alpha = 1.0;

    /* Enable antialiasing by default */
    brush->sampling_flag |= BRUSH_PAINT_ANTIALIASING;
  }

  {
    /* Change the spacing of the Smear brush to 3.0% */
    const char *brush_name;
    Brush *brush;

    brush_name = "Smear";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (brush) {
      brush->spacing = 3.0;
    }

    brush_name = "Draw Sharp";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_DRAW_SHARP;
    }

    brush_name = "Elastic Deform";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_ELASTIC_DEFORM;
    }

    brush_name = "Pose";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_POSE;
    }

    brush_name = "Multi-plane Scrape";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_MULTIPLANE_SCRAPE;
    }

    brush_name = "Clay Thumb";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_CLAY_THUMB;
    }

    brush_name = "Cloth";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_CLOTH;
    }

    brush_name = "Slide Relax";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_SLIDE_RELAX;
    }

    brush_name = "Paint";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_PAINT;
    }

    brush_name = "Smear";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_SMEAR;
    }

    brush_name = "Boundary";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_BOUNDARY;
    }

    brush_name = "Simplify";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_SIMPLIFY;
    }

    brush_name = "Draw Face Sets";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_DRAW_FACE_SETS;
    }

    brush_name = "Multires Displacement Eraser";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_DISPLACEMENT_ERASER;
    }

    brush_name = "Multires Displacement Smear";
    brush = static_cast<Brush *>(
        BLI_findstring(&bmain->brushes, brush_name, offsetof(ID, name) + 2));
    if (!brush) {
      brush = BKE_brush_add(bmain, brush_name, OB_MODE_SCULPT);
      id_us_min(&brush->id);
      brush->sculpt_tool = SCULPT_TOOL_DISPLACEMENT_SMEAR;
    }

    /* Use the same tool icon color in the brush cursor */
    LISTBASE_FOREACH (Brush *, brush, &bmain->brushes) {
      if (brush->ob_mode & OB_MODE_SCULPT) {
        BLI_assert(brush->sculpt_tool != 0);
        BKE_brush_sculpt_reset(brush);
      }
    }
  }
}
