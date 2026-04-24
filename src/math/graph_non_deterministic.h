#ifndef __AFC_EDITOR_MATH_GRAPH_NON_DETERMINISTIC_H__
#define __AFC_EDITOR_MATH_GRAPH_NON_DETERMINISTIC_H__

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

#include "src/language/ghost_type_class.h"

namespace afc::math::graph_non_deterministic {
struct NodeId : public language::GhostType<NodeId, size_t> {
  using GhostType::GhostType;
};

using NodeGroup = std::set<NodeId>;

template <typename EdgeValue, typename NodeValue>
struct Node {
  std::map<EdgeValue, NodeGroup> edges = {};

  // This node automatically reaches the following nodes (without consuing
  // characters).
  NodeGroup automatic_edges;

  // On any EdgeValue, this node also reaches these nodes.
  NodeGroup any_value_edges;

  NodeValue value;
};

template <typename EdgeValue, typename NodeValue>
struct Graph
    : public language::GhostType<Graph<EdgeValue, NodeValue>,
                                 std::vector<Node<EdgeValue, NodeValue>>> {
  using NodeId = graph_non_deterministic::NodeId;
  using NodeGroup = std::set<NodeId>;
  using Node = graph_non_deterministic::Node<EdgeValue, NodeValue>;
  using Base =
      language::GhostType<Graph<EdgeValue, NodeValue>, std::vector<Node>>;
  using Base::Base;

  const Node& at(const NodeId& key) const { return Base::at(key.read()); }

  NodeGroup GetAutomaticClosure(NodeGroup states) const {
    std::vector<NodeId> pending(std::from_range, states);
    while (!pending.empty()) {
      NodeId current = pending.back();
      pending.pop_back();
      CHECK_LT(current.read(), this->size());
      std::ranges::for_each(this->at(current).automatic_edges,
                            [&](const NodeId& id) {
                              if (states.insert(id).second)
                                pending.push_back(id);
                            });
    }
    return states;
  }
};
}  // namespace afc::math::graph_non_deterministic

#endif  // __AFC_EDITOR_MATH_GRAPH_NON_DETERMINISTIC_H__
