/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_function_util.hh"

#include "BLI_hash.h"

#include "UI_interface.h"
#include "UI_resources.h"

namespace blender::nodes::node_fn_input_int_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Int>("Integer");
}

static void node_layout(uiLayout *layout, bContext * /*C*/, PointerRNA *ptr)
{
  uiLayout *col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "integer", UI_ITEM_R_EXPAND, "", ICON_NONE);
}

static void node_build_multi_function(NodeMultiFunctionBuilder &builder)
{
  const bNode &bnode = builder.node();
  NodeInputInt *node_storage = static_cast<NodeInputInt *>(bnode.storage);
  builder.construct_and_set_matching_fn<mf::CustomMF_Constant<int>>(node_storage->integer);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  NodeInputInt *data = MEM_cnew<NodeInputInt>(__func__);
  node->storage = data;
}

}  // namespace blender::nodes::node_fn_input_int_cc

void register_node_type_fn_input_int()
{
  namespace file_ns = blender::nodes::node_fn_input_int_cc;

  static bNodeType ntype;

  fn_node_type_base(&ntype, FN_NODE_INPUT_INT, "Integer", 0);
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_init;
  node_type_storage(
      &ntype, "NodeInputInt", node_free_standard_storage, node_copy_standard_storage);
  ntype.build_multi_function = file_ns::node_build_multi_function;
  ntype.draw_buttons = file_ns::node_layout;
  nodeRegisterType(&ntype);
}