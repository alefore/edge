#ifndef __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
#define __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__

#include <map>
#include <optional>

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/column_number.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/math/graph_deterministic.h"

namespace afc::infrastructure {
class GlobMatcher {
 public:
  enum class PatternType { Special, Literal };

 private:
  const language::lazy_string::LazyString pattern_;
  using Graph = afc::math::graph_deterministic::Graph<
      wchar_t, language::lazy_string::ColumnNumberDelta>;
  const Graph graph_;
  const PatternType pattern_type_;

 public:
  static GlobMatcher New(language::lazy_string::LazyString pattern);

  language::lazy_string::LazyString pattern() const;

  PatternType pattern_type() const;

  struct MatchResults {
    // Length of the longest prefix of `pattern` that matches a prefix
    // of `component`.
    language::lazy_string::ColumnNumberDelta pattern_prefix_size;

    // Length of the longest prefix of `component` that is matched by the
    // longest prefix of `pattern` that matches something.
    language::lazy_string::ColumnNumberDelta component_prefix_size;

    enum class MatchType {
      // Could not match the entirety of the pattern to the input.
      None,
      // Matched the entirety of the pattern to the input, but the input
      // containing unconsumed training characters.
      Partial,
      // The pattern matched the input exactly.
      Exact
    };
    MatchType match_type;
  };

  MatchResults Match(PathComponent component) const;
  MatchResults Match(language::lazy_string::LazyString input) const;

 private:
  GlobMatcher(language::lazy_string::LazyString pattern, Graph graph,
              PatternType pattern_type);
};

std::ostream& operator<<(std::ostream& os,
                         const GlobMatcher::MatchResults& value);
}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
