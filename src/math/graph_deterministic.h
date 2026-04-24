#ifndef __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__
#define __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

#include "src/language/ghost_type_class.h"
#include "src/math/graph_non_deterministic.h"

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
};

template <typename EdgeValue, typename NDGNodeValue, typename Callable>
auto FromNonDeterministic(
    graph_non_deterministic::Graph<EdgeValue, NDGNodeValue> ndg,
    Callable value_aggregator) {
  using NDG = decltype(ndg);
  std::map<typename NDG::NodeGroup, NodeId> nd_groups;
  using NodeValue = std::invoke_result_t<Callable, std::vector<NDGNodeValue>>;

  Graph<EdgeValue, NodeValue> output;
  std::vector<typename NDG::NodeGroup> pending;

  auto get_or_create_node = [&](NDG::NodeGroup group) -> NodeId {
    group = ndg.GetAutomaticClosure(std::move(group));
    if (auto it = nd_groups.find(group); it != nd_groups.end())
      return it->second;
    NodeId node_id = NodeId{output.size()};
    nd_groups[group] = node_id;
    pending.push_back(group);
    output.push_back(Node<EdgeValue, NodeValue>{
        .value = value_aggregator(group |
                                  std::views::transform([&ndg](NDG::NodeId i) {
                                    return ndg.at(i).value;
                                  }) |
                                  std::ranges::to<std::vector>())});
    return node_id;
  };

  get_or_create_node(typename NDG::NodeGroup{typename NDG::NodeId{0}});

  while (!pending.empty()) {
    typename NDG::NodeGroup nd_group = pending.back();
    pending.pop_back();
    CHECK(nd_groups.find(nd_group) != nd_groups.end());
    CHECK_LT(nd_groups[nd_group], output.size());
    NodeId node_id = nd_groups[nd_group];
    std::map<EdgeValue, typename NDG::NodeGroup> value_edges = {};
    for (typename NDG::NodeId nd_node : nd_group) {
      CHECK_LT(nd_node, ndg.size());
      for (std::pair<EdgeValue, typename NDG::NodeGroup> item :
           ndg[nd_node.read()].edges)
        value_edges[item.first].insert_range(item.second);
    }
    typename NDG::NodeGroup any_value_edges =
        nd_group | std::views::transform([&](typename NDG::NodeId nd_node) {
          return ndg[nd_node.read()].any_value_edges;
        }) |
        std::views::join | std::ranges::to<typename NDG::NodeGroup>();
    for (std::pair<EdgeValue, typename NDG::NodeGroup> item : value_edges) {
      typename NDG::NodeGroup combined_target = item.second;
      combined_target.insert_range(any_value_edges);
      output[node_id.read()].edges[item.first] =
          get_or_create_node(combined_target);
    }

    if (!any_value_edges.empty())
      output[node_id.read()].default_edge = get_or_create_node(any_value_edges);
  }
  return output;
}
}  // namespace afc::math::graph_deterministic

#endif  // __AFC_EDITOR_MATH_GRAPH_DETERMINISTIC_H__
