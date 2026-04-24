#include "src/infrastructure/glob.h"

#include <algorithm>
#include <queue>
#include <ranges>
#include <set>

#include "src/language/lazy_string/functional.h"
#include "src/math/graph_deterministic.h"
#include "src/math/graph_non_deterministic.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

using NDG =
    afc::math::graph_non_deterministic::Graph<wchar_t, ColumnNumberDelta>;
using DG = afc::math::graph_deterministic::Graph<wchar_t, ColumnNumberDelta>;

namespace afc::infrastructure {

std::pair<NDG, GlobMatcher::PatternType> BuildNDGraph(LazyString pattern) {
  std::vector<NDG::Node> output(pattern.size().read() + 1);
  GlobMatcher::PatternType output_type = GlobMatcher::PatternType::Literal;
  ForEachColumn(pattern, [&](ColumnNumber i, wchar_t c) {
    NDG::NodeId index{i.read()};
    NDG::NodeId next = NDG::NodeId{index.read() + 1};
    NDG::Node& node = output[index.read()];
    node.value = i.ToDelta();
    switch (c) {
      case '*':
        node.any_value_edges.insert(index);
        node.automatic_edges.insert(next);
        output_type = GlobMatcher::PatternType::Special;
        break;
      case '?':
        output_type = GlobMatcher::PatternType::Special;
        node.any_value_edges.insert(next);
        break;
      default:
        node.edges[c].insert(next);
    }
  });
  output[pattern.size().read()].value = pattern.size();
  return {output, output_type};
}

/* static */ GlobMatcher GlobMatcher::New(
    language::lazy_string::LazyString pattern) {
  std::pair<NDG, PatternType> data = BuildNDGraph(pattern);
  return GlobMatcher(
      pattern,
      data.first.ToDeterministic([](std::vector<ColumnNumberDelta> values) {
        return std::ranges::max(values);
      }),
      data.second);
}

GlobMatcher::GlobMatcher(LazyString pattern, Graph graph,
                         PatternType pattern_type)
    : pattern_(pattern),
      graph_(std::move(graph)),
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
  using DG = decltype(graph_);
  typename DG::NodeId current_index{0};
  ColumnNumberDelta input_index;
  for (; input_index < input.size(); ++input_index) {
    wchar_t c = input.get(ColumnNumber{} + input_index);
    const typename DG::Node& current_node = graph_.at(current_index);
    if (auto it = current_node.edges.find(c); it != current_node.edges.end())
      current_index = it->second;
    else if (current_node.default_edge.has_value())
      current_index = current_node.default_edge.value();
    else {
      break;
    }
  }
  ColumnNumberDelta pattern_prefix_size = graph_.at(current_index).value;
  MatchResults output{.pattern_prefix_size = pattern_prefix_size,
                      .component_prefix_size = input_index,
                      .match_type = std::invoke([&] {
                        if (pattern_prefix_size < pattern_.size())
                          return MatchResults::MatchType::None;
                        if (input_index < input.size())
                          return MatchResults::MatchType::Partial;
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
