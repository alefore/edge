#ifndef __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__
#define __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

#include "src/language/ghost_type_class.h"

namespace afc::math::graph_deterministic {
struct NodeId : public language::GhostType<NodeId, size_t> {
  using GhostType::GhostType;
};

// Each Node represents a parsing state.
template <typename EdgeValue, typename NodeValue>
struct Node {
  // If a value is present, represents the transition.
  std::map<EdgeValue, NodeId> edges = {};
  // If present, used for values not in `edges`.
  std::optional<NodeId> default_edge = std::nullopt;
  NodeValue value;
  // language::lazy_string::ColumnNumberDelta pattern_prefix_size;
};

template <typename EdgeValue, typename NodeValue>
struct Graph
    : public language::GhostType<Graph<EdgeValue, NodeValue>,
                                 std::vector<Node<EdgeValue, NodeValue>>> {
  using NodeId = graph_deterministic::NodeId;
  using Node = graph_deterministic::Node<EdgeValue, NodeValue>;
  using Base =
      language::GhostType<Graph<EdgeValue, NodeValue>, std::vector<Node>>;
  using Base::Base;

  const Node& at(const NodeId& key) const { return Base::at(key.read()); }
  auto& operator[](const NodeId& key) { return Base::operator[](key.read()); }
};
}  // namespace afc::math::graph_deterministic

#endif  // __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__
