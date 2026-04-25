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

using DG = afc::math::graph_deterministic::Graph<wchar_t, ColumnNumberDelta>;

namespace afc::infrastructure {

/* static */
std::pair<GlobMatcher::NDGraph, std::vector<GlobMatcher::PatternType>>
GlobMatcher::BuildNDGraph(std::vector<LazyString> patterns) {
  std::vector<PatternType> pattern_types;
  std::vector<NDGraph> graphs =
      patterns | std::views::transform([&pattern_types](LazyString pattern) {
        NDGraph output;
        PatternType output_type = PatternType::Literal;
        ForEachColumn(pattern, [&](ColumnNumber i, wchar_t c) {
          NDGraph::NodeId index{i.read()};
          NDGraph::NodeId next = NDGraph::NodeId{index.read() + 1};
          NDGraph::Node node{
              .value = MatchState{.pattern = pattern, .position = i.ToDelta()}};
          switch (c) {
            case '*':
              node.any_value_edges.insert(index);
              node.automatic_edges.insert(next);
              output_type = PatternType::Special;
              break;
            case '?':
              output_type = PatternType::Special;
              node.any_value_edges.insert(next);
              break;
            default:
              node.edges[c].insert(next);
          }
          output.push_back(std::move(node));
        });
        output.push_back(
            NDGraph::Node{.value = MatchState{.pattern = pattern,
                                              .position = pattern.size()}});
        pattern_types.push_back(output_type);
        return output;
      }) |
      std::ranges::to<std::vector>();
  CHECK_EQ(graphs.size(), pattern_types.size());
  return {NDGraph::Merge(std::move(graphs)), pattern_types};
}

/* static */ std::vector<GlobMatcher::MatchState> GlobMatcher::GetMinSuffixSize(
    std::vector<MatchState> input) {
  auto key = [](const MatchState& s) { return s.suffix_size(); };
  ColumnNumberDelta min_key = key(*std::ranges::min_element(input, {}, key));
  return input | std::views::filter([key, min_key](const MatchState& s) {
           return key(s) == min_key;
         }) |
         std::ranges::to<std::vector>();
}

/* static */ GlobMatcher GlobMatcher::New(
    std::vector<language::lazy_string::LazyString> patterns) {
  CHECK(!patterns.empty()) << "Can't create a GlobMatcher with no patterns.";
  std::pair<NDGraph, std::vector<PatternType>> data = BuildNDGraph(patterns);
  CHECK_EQ(data.second.size(), patterns.size());
  return GlobMatcher(patterns,
                     std::move(data.first).ToDeterministic(GetMinSuffixSize),
                     data.second);
}

GlobMatcher::GlobMatcher(std::vector<LazyString> patterns, Graph graph,
                         std::vector<PatternType> pattern_types)
    : patterns_(patterns),
      graph_(std::move(graph)),
      pattern_types_(pattern_types) {}

std::vector<LazyString> GlobMatcher::patterns() const { return patterns_; }

std::vector<GlobMatcher::PatternType> GlobMatcher::pattern_types() const {
  return pattern_types_;
}

GlobMatcher::MatchResults GlobMatcher::Match(LazyString input) const {
  VLOG(6) << "Match " << patterns_.size() << " (eg: " << patterns_[0]
          << ") to: " << input;
  typename Graph::NodeId current_index{0};
  ColumnNumberDelta input_index;
  for (; input_index < input.size(); ++input_index) {
    wchar_t c = input.get(ColumnNumber{} + input_index);
    const typename Graph::Node& current_node = graph_.at(current_index);
    if (auto it = current_node.edges.find(c); it != current_node.edges.end())
      current_index = it->second;
    else if (current_node.default_edge.has_value())
      current_index = current_node.default_edge.value();
    else {
      break;
    }
  }
  std::vector<MatchState> patterns =
      GetMinSuffixSize(graph_.at(current_index).value);
  CHECK(!patterns.empty());
  ColumnNumberDelta suffix_size = patterns[0].suffix_size();
  CHECK(std::ranges::all_of(patterns, [&suffix_size](const MatchState& e) {
    return e.suffix_size() == suffix_size;
  }));
  MatchResults output{.patterns = std::move(patterns) |
                                  std::views::transform(&MatchState::pattern) |
                                  std::ranges::to<std::vector>(),
                      .patterns_suffix_size = suffix_size,
                      .component_prefix_size = input_index,
                      .match_type = std::invoke([&] {
                        if (!suffix_size.IsZero())
                          return MatchResults::MatchType::None;
                        if (input_index < input.size())
                          return MatchResults::MatchType::Partial;
                        return MatchResults::MatchType::Exact;
                      })};
  VLOG(2) << "Match: " << output;
  return output;
}

std::ostream& operator<<(std::ostream& os,
                         const GlobMatcher::MatchResults& value) {
  os << "[MatchResult: patterns_suffix_size=" << value.patterns_suffix_size
     << ", component_prefix_size=" << value.component_prefix_size << "]";
  return os;
}

SinglePatternGlobMatcher::SinglePatternGlobMatcher(
    language::lazy_string::LazyString pattern)
    : glob_matcher_(GlobMatcher::New({pattern})) {}

language::lazy_string::LazyString SinglePatternGlobMatcher::pattern() const {
  return glob_matcher_.patterns()[0];
}

GlobMatcher::PatternType SinglePatternGlobMatcher::pattern_type() const {
  return glob_matcher_.pattern_types()[0];
}

GlobMatcher::MatchResults SinglePatternGlobMatcher::Match(
    language::lazy_string::LazyString input) const {
  return glob_matcher_.Match(std::move(input));
}

}  // namespace afc::infrastructure
