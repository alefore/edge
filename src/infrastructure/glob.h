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
  struct NodeId : public language::GhostType<NodeId, size_t> {
    using GhostType::GhostType;
  };

  // Each Node represents a parsing state.
  struct Node {
    // If a char is present, represents the transition.
    std::map<wchar_t, NodeId> edges = {};
    // If present, used for characters not in `edges`.
    std::optional<NodeId> default_edge = std::nullopt;
    language::lazy_string::ColumnNumberDelta pattern_prefix_length;
  };
  const std::vector<Node> nodes_;

 public:
  GlobMatcher(language::lazy_string::LazyString pattern);

  struct MatchResults {
    // Length of the longest prefix of `pattern` that matches a prefix
    // of `component`.
    language::lazy_string::ColumnNumberDelta pattern_prefix_length;

    // Length of the longest prefix of `component` that is matched by the
    // longest prefix of `pattern` that matches something.
    language::lazy_string::ColumnNumberDelta component_prefix_length;
  };

  MatchResults Match(PathComponent component);

 private:
  static std::vector<Node> NodesForPattern(
      language::lazy_string::LazyString pattern);

  friend std::ostream& operator<<(std::ostream& os,
                                  const GlobMatcher::Node& value);
};
}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_INFRASTRUCTURE_GLOB_H__
