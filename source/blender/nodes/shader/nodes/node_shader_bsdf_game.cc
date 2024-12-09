#include "node_shader_util.hh"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_define.h"
#include "BKE_node_runtime.hh"

namespace blender::nodes::node_shader_bsdf_game_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Color").default_value({1.0f, 1.0f, 1.0f, 1.0f});
  b.add_input<decl::Float>("Metallic")
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Specular")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Roughness")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Alpha")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Vector>("Normal").hide_value();
  b.add_input<decl::Float>("Weight").unavailable();
  b.add_output<decl::Shader>("BSDF");
}

static void node_shader_init_game(bNodeTree * /*ntree*/, bNode *node)
{
  // Initialisation des valeurs par défaut si nécessaire
  node->custom1 = 0; // Exemple d'initialisation
}

static int node_shader_gpu_bsdf_game(GPUMaterial *mat,
                                      bNode *node,
                                      bNodeExecData * /*execdata*/,
                                      GPUNodeStack *in,
                                      GPUNodeStack *out)
{
  // Récupération des normales
  if (!in[5].link) {
    GPU_link(mat, "world_normals_get", &in[5].link);
  }
  // Définir le drapeau du matériau
  GPU_material_flag_set(mat, GPU_MATFLAG_DIFFUSE);
  // Lier les entrées et sorties au shader
  return GPU_stack_link(mat, node, "node_shader_bsdf_game", in, out);
}

static void node_shader_update_game(bNodeTree *ntree, bNode *node)
{
  // Mettre à jour la disponibilité des sockets si nécessaire
  // Par exemple, vous pouvez désactiver certains sockets en 
  // fonction des valeurs d'autres sockets
}

} // namespace blender::nodes::node_shader_bsdf_game_cc

/* node type definition */
void register_node_type_sh_bsdf_game()
{
  namespace file_ns = blender::nodes::node_shader_bsdf_game_cc;

  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_BSDF_GAME, "BSDF Game", NODE_CLASS_SHADER);
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_shader_init_game; // Ajout de la fonction d'initialisation
  ntype.add_ui_poll = object_shader_nodes_poll;
  blender::bke::node_type_size_preset(&ntype, blender::bke::eNodeSizePreset::MIDDLE);
  ntype.gpu_fn = file_ns::node_shader_gpu_bsdf_game;
  ntype.updatefunc = file_ns::node_shader_update_game; // Ajout de la fonction de mise à jour

  nodeRegisterType(&ntype);
}
