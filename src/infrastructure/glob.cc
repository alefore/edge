#include "src/infrastructure/glob.h"

#include <algorithm>
#include <queue>
#include <ranges>
#include <set>

#include "src/language/lazy_string/functional.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

namespace afc::infrastructure {
struct NDNodeId : public language::GhostType<NDNodeId, size_t> {
  using GhostType::GhostType;
};

using NDNodeGroup = std::set<NDNodeId>;

struct NDNode {
  std::map<wchar_t, std::vector<NDNodeId>> edges = {};

  // This node automatically reaches the following nodes (without consuing
  // characters).
  std::vector<NDNodeId> automatic_edges;

  // On any character, this node also reaches these edges.
  std::vector<NDNodeId> any_char_edges;
};

using NDGraph = std::vector<NDNode>;

std::pair<NDGraph, GlobMatcher::PatternType> BuildNDGraph(LazyString pattern) {
  std::vector<NDNode> output(pattern.size().read() + 1);
  GlobMatcher::PatternType output_type = GlobMatcher::PatternType::Literal;
  ForEachColumn(pattern, [&](ColumnNumber i, wchar_t c) {
    NDNodeId index{i.read()};
    NDNodeId next = NDNodeId{index.read() + 1};
    NDNode& node = output[index.read()];
    switch (c) {
      case '*':
        node.any_char_edges.push_back(index);
        node.automatic_edges.push_back(next);
        output_type = GlobMatcher::PatternType::Special;
        break;
      case '?':
        output_type = GlobMatcher::PatternType::Special;
        node.any_char_edges.push_back(next);
        break;
      default:
        node.edges[c].push_back(next);
    }
  });
  return {output, output_type};
}

NDNodeGroup GetAutomaticClosure(const NDGraph& graph, NDNodeGroup states) {
  NDNodeGroup output = states;
  std::queue<NDNodeId> queue;
  for (auto& id : states) queue.push(id);
  while (!queue.empty()) {
    NDNodeId current = queue.front();
    CHECK_LT(current.read(), graph.size());
    queue.pop();
    std::ranges::for_each(graph[current.read()].automatic_edges,
                          [&](const NDNodeId& id) {
                            if (output.insert(id).second) queue.push(id);
                          });
  }
  return output;
}

/* static */
std::pair<std::vector<GlobMatcher::Node>, GlobMatcher::PatternType>
GlobMatcher::NodesForPattern(LazyString pattern) {
  std::pair<std::vector<NDNode>, PatternType> tmp_graph = BuildNDGraph(pattern);
  VLOG(4) << "ND Match states: " << tmp_graph.first.size();
  std::map<NDNodeGroup, NodeId> nd_groups;

  std::vector<Node> output;

  std::queue<NDNodeGroup> queue;

  auto get_or_create_node = [&](NDNodeGroup group) -> NodeId {
    group = GetAutomaticClosure(tmp_graph.first, group);
    if (auto it = nd_groups.find(group); it != nd_groups.end())
      return it->second;
    NodeId node_id = NodeId{output.size()};
    nd_groups[group] = node_id;
    queue.push(group);
    output.push_back(Node{.pattern_prefix_size = ColumnNumberDelta{
                              static_cast<int>(group.rbegin()->read())}});
    return node_id;
  };

  get_or_create_node(NDNodeGroup{NDNodeId{0}});

  while (!queue.empty()) {
    NDNodeGroup nd_group = queue.front();
    queue.pop();
    CHECK(nd_groups.find(nd_group) != nd_groups.end());
    CHECK_LT(nd_groups[nd_group], output.size());
    NodeId node_id = nd_groups[nd_group];
    std::map<wchar_t, NDNodeGroup> char_edges = {};
    for (NDNodeId nd_node : nd_group) {
      CHECK_LT(nd_node, tmp_graph.first.size());
      for (std::pair<wchar_t, std::vector<NDNodeId>> item :
           tmp_graph.first[nd_node.read()].edges)
        char_edges[item.first].insert_range(item.second);
    }
    NDNodeGroup any_char_edges =
        nd_group | std::views::transform([&](NDNodeId nd_node) {
          return tmp_graph.first[nd_node.read()].any_char_edges;
        }) |
        std::views::join | std::ranges::to<NDNodeGroup>();
    for (std::pair<wchar_t, NDNodeGroup> item : char_edges) {
      NDNodeGroup combined_target = item.second;
      combined_target.insert_range(any_char_edges);
      output[node_id.read()].edges[item.first] =
          get_or_create_node(combined_target);
    }

    if (!any_char_edges.empty())
      output[node_id.read()].default_edge = get_or_create_node(any_char_edges);
  }
  return {output, tmp_graph.second};
}

std::ostream& operator<<(std::ostream& os, const GlobMatcher::Node& value) {
  os << "[Node with edges " << value.edges.size() << "]";
  return os;
}

/* static */ GlobMatcher GlobMatcher::New(
    language::lazy_string::LazyString pattern) {
  std::pair<std::vector<Node>, PatternType> data = NodesForPattern(pattern);
  return GlobMatcher(pattern, std::move(data.first), data.second);
}

GlobMatcher::GlobMatcher(LazyString pattern, std::vector<Node> nodes,
                         PatternType pattern_type)
    : pattern_(pattern),
      nodes_(std::move(nodes)),
      pattern_type_(pattern_type) {}

language::lazy_string::LazyString GlobMatcher::pattern() const {
  return pattern_;
}

GlobMatcher::PatternType GlobMatcher::pattern_type() const {
  return pattern_type_;
}

GlobMatcher::MatchResults GlobMatcher::Match(PathComponent component) const {
  return Match(ToLazyString(component));
}

GlobMatcher::MatchResults GlobMatcher::Match(LazyString input) const {
  VLOG(6) << "Match " << pattern_ << " to: " << input;
  NodeId current_index{0};
  ColumnNumberDelta input_index;
  for (; input_index < input.size(); ++input_index) {
    wchar_t c = input.get(ColumnNumber{} + input_index);
    const Node& current_node = nodes_[current_index.read()];
    if (auto it = current_node.edges.find(c); it != current_node.edges.end())
      current_index = it->second;
    else if (current_node.default_edge.has_value())
      current_index = current_node.default_edge.value();
    else {
      break;
    }
  }
  MatchResults output{
      .pattern_prefix_size = nodes_[current_index.read()].pattern_prefix_size,
      .component_prefix_size =
          ColumnNumberDelta{static_cast<int>(input_index.read())},
      .match_type = std::invoke([&] {
        if (nodes_[current_index.read()].pattern_prefix_size < pattern_.size())
          return MatchResults::MatchType::None;
        if (input_index < input.size()) return MatchResults::MatchType::Partial;
        return MatchResults::MatchType::Exact;
      })};
  VLOG(2) << "Match " << pattern_ << " to: " << input << ": " << output;
  return output;
}

std::ostream& operator<<(std::ostream& os,
                         const GlobMatcher::MatchResults& value) {
  os << "[MatchResult: pattern_prefix_size=" << value.pattern_prefix_size
     << ", component_prefix_size=" << value.component_prefix_size << "]";
  return os;
}
}  // namespace afc::infrastructure
