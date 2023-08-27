#ifndef __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
#define __AFC_EDITOR_BUFFER_SYNTAX_PARSER__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"
#include "src/futures/delete_notification.h"
#include "src/language/observers.h"
#include "src/language/safe_types.h"
#include "src/parse_tree.h"

namespace afc::editor {
// This class is thread-safe (and does significant work in a background thread).
class BufferSyntaxParser {
 public:
  struct ParserOptions {
    std::wstring parser_name;
    std::unordered_set<std::wstring> typos_set;
    std::unordered_set<std::wstring> language_keywords;
    std::wstring symbol_characters;
    IdentifierBehavior identifier_behavior;
  };
  void UpdateParser(ParserOptions options);

  void Parse(language::NonNull<std::unique_ptr<BufferContents>> contents);

  language::NonNull<std::shared_ptr<const ParseTree>> tree() const;
  language::NonNull<std::shared_ptr<const ParseTree>> simplified_tree() const;

  language::NonNull<std::shared_ptr<const ParseTree>>
  current_zoomed_out_parse_tree(
      language::text::LineNumberDelta view_size,
      language::text::LineNumberDelta lines_size) const;

  language::Observable& ObserveTrees();

 private:
  mutable concurrent::ThreadPool thread_pool_ =
      concurrent::ThreadPool(1, nullptr);

  struct Data {
    // When the tree changes, we replace this and schedule in `syntax_data_` new
    // work.
    language::NonNull<std::unique_ptr<futures::DeleteNotification>>
        cancel_state;

    language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
        NewNullTreeParser();

    language::NonNull<std::shared_ptr<const ParseTree>> tree =
        language::MakeNonNullShared<const ParseTree>(language::text::Range());

    language::NonNull<std::shared_ptr<const ParseTree>> simplified_tree =
        language::MakeNonNullShared<const ParseTree>(language::text::Range());

    // Caches the last parse done (by syntax_data_zoom_) for a given view size.
    struct ZoomedOutTreeData {
      // The input parse tree from which zoomed_out_parse_tree was computed.
      // This is kept so that we can detect when the parse tree has changed and
      // thus we need to start updating the zoomed_out_parse_tree (if the view
      // is still active).
      language::NonNull<std::shared_ptr<const ParseTree>> simplified_tree;
      language::NonNull<std::shared_ptr<const ParseTree>> zoomed_out_tree;
    };
    mutable std::unordered_map<language::text::LineNumberDelta,
                               ZoomedOutTreeData>
        zoomed_out_trees;
  };

  const language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_;
  const language::NonNull<std::shared_ptr<language::Observers>> observers_;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
