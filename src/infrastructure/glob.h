#ifndef __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
#define __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__

#include <map>
#include <optional>

#include "src/infrastructure/dirname.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/column_number.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::infrastructure {
class GlobMatcher {
 public:
  enum class PatternType { Special, Literal };

 private:
  struct NodeId : public language::GhostType<NodeId, size_t> {
    using GhostType::GhostType;
  };

  // Each Node represents a parsing state.
  struct Node {
    // If a char is present, represents the transition.
    std::map<wchar_t, NodeId> edges = {};
    // If present, used for characters not in `edges`.
    std::optional<NodeId> default_edge = std::nullopt;
    language::lazy_string::ColumnNumberDelta pattern_prefix_size;
  };
  const language::lazy_string::LazyString pattern_;
  const std::vector<Node> nodes_;
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
  };

  MatchResults Match(PathComponent component) const;

 private:
  static std::pair<std::vector<Node>, PatternType> NodesForPattern(
      language::lazy_string::LazyString pattern);

  GlobMatcher(language::lazy_string::LazyString pattern,
              std::vector<Node> graph, PatternType pattern_type);

  friend std::ostream& operator<<(std::ostream& os,
                                  const GlobMatcher::Node& value);
};

std::ostream& operator<<(std::ostream& os,
                         const GlobMatcher::MatchResults& value);
}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
