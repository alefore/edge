#ifndef __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
#define __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__

#include <map>
#include <optional>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/column_number.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/math/graph_deterministic.h"
#include "src/math/graph_non_deterministic.h"

namespace afc::infrastructure {
class GlobMatcher {
 public:
  enum class PatternType { Special, Literal };

 private:
  struct MatchState {
    language::lazy_string::LazyString pattern;
    language::lazy_string::ColumnNumberDelta position;

    language::lazy_string::ColumnNumberDelta suffix_size() const {
      CHECK_GE(pattern.size(), position);
      return pattern.size() - position;
    }
  };

  using NDGraph =
      afc::math::graph_non_deterministic::Graph<wchar_t, MatchState>;
  using Graph =
      afc::math::graph_deterministic::Graph<wchar_t, std::vector<MatchState>>;

  const std::vector<language::lazy_string::LazyString> patterns_;
  const Graph graph_;
  const std::vector<PatternType> pattern_types_;

 public:
  static GlobMatcher New(
      std::vector<language::lazy_string::LazyString> patterns);

  std::vector<language::lazy_string::LazyString> patterns() const;

  std::vector<PatternType> pattern_types() const;

  struct MatchResults {
    // The patterns with minimal `patterns_suffix_size` among the patterns with
    // maximal `component_prefix_size`. In other words, among the patterns that
    // consumed the longest prefix of the input, those with the minimal
    // (unmatched) pattern suffix.
    std::vector<language::lazy_string::LazyString> patterns;
    language::lazy_string::ColumnNumberDelta patterns_suffix_size;

    // Length of the longest prefix of `component` that is matched by the
    // longest prefix of `pattern` that matches something.
    language::lazy_string::ColumnNumberDelta component_prefix_size;

    enum class MatchType {
      // No pattern could be fully matched to a prefix of the input (i.e.,
      // `pattern_suffix_size` is non-zero).
      None,
      // The input had characters that no pattern accepted (i.e.,
      // `component_prefix_size` is non-zero).
      Partial,
      // The pattern and the input matched fully (i.e., both
      // `pattern_suffix_size` and `component_prefix_size` are zero).
      Exact
    };
    MatchType match_type;
  };

  MatchResults Match(language::lazy_string::LazyString input) const;

 private:
  GlobMatcher(std::vector<language::lazy_string::LazyString> pattern,
              Graph graph, std::vector<PatternType> pattern_types);

  static std::pair<NDGraph, std::vector<PatternType>> BuildNDGraph(
      std::vector<afc::language::lazy_string::LazyString> patterns);

  static std::vector<MatchState> GetMinSuffixSize(
      std::vector<MatchState> input);
};

std::ostream& operator<<(std::ostream& os,
                         const GlobMatcher::MatchResults& value);

// Convenience wrapper for the common case of a single pattern.
class SinglePatternGlobMatcher {
 private:
  GlobMatcher glob_matcher_;

 public:
  SinglePatternGlobMatcher(language::lazy_string::LazyString pattern);
  language::lazy_string::LazyString pattern() const;
  GlobMatcher::PatternType pattern_type() const;
  GlobMatcher::MatchResults Match(
      language::lazy_string::LazyString input) const;
};

}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
