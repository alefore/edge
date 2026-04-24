#ifndef __AFC_EDITOR_MATH_GRAPH_NON_DETERMINISTIC_H__
#define __AFC_EDITOR_MATH_GRAPH_NON_DETERMINISTIC_H__

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

#include "src/language/ghost_type_class.h"
#include "src/math/graph_deterministic.h"

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
  template <typename Self>
  auto& operator[](this Self&& self, const NodeId& key) {
    return self->Base::operator[](key);
  }

  template <typename Callable>
  auto ToDeterministic(Callable value_aggregator) const {
    using OutputNodeValue =
        std::invoke_result_t<Callable, std::vector<NodeValue>>;
    graph_deterministic::Graph<EdgeValue, OutputNodeValue> output;

    using OutputNodeId = typename decltype(output)::NodeId;
    std::map<NodeGroup, OutputNodeId> nd_groups;
    std::vector<NodeGroup> pending;

    auto get_or_create_node = [&](NodeGroup group) -> OutputNodeId {
      group = GetAutomaticClosure(std::move(group));
      if (auto it = nd_groups.find(group); it != nd_groups.end())
        return it->second;
      OutputNodeId node_id{output.size()};
      nd_groups[group] = node_id;
      pending.push_back(group);
      output.push_back(typename decltype(output)::Node{
          .value =
              value_aggregator(group | std::views::transform([this](NodeId i) {
                                 return this->at(i).value;
                               }) |
                               std::ranges::to<std::vector>())});
      return node_id;
    };

    get_or_create_node(NodeGroup{NodeId{0}});

    while (!pending.empty()) {
      NodeGroup nd_group = pending.back();
      pending.pop_back();
      CHECK(nd_groups.find(nd_group) != nd_groups.end());
      CHECK_LT(nd_groups[nd_group], output.size());
      OutputNodeId node_id = nd_groups[nd_group];
      std::map<EdgeValue, NodeGroup> value_edges = {};
      for (NodeId node : nd_group) {
        CHECK_LT(node, this->size());
        for (std::pair<EdgeValue, NodeGroup> item : this->at(node).edges)
          value_edges[item.first].insert_range(item.second);
      }
      NodeGroup any_value_edges =
          nd_group | std::views::transform([&](NodeId nd_node) {
            return this->at(nd_node).any_value_edges;
          }) |
          std::views::join | std::ranges::to<NodeGroup>();
      for (std::pair<EdgeValue, NodeGroup> item : value_edges) {
        NodeGroup combined_target = item.second;
        combined_target.insert_range(any_value_edges);
        output[node_id].edges[item.first] = get_or_create_node(combined_target);
      }

      if (!any_value_edges.empty())
        output[node_id].default_edge = get_or_create_node(any_value_edges);
    }
    return output;
  }

 private:
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
