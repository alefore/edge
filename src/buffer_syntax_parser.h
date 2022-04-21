#ifndef __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
#define __AFC_EDITOR_BUFFER_SYNTAX_PARSER__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/concurrent/notification.h"
#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"
#include "src/cpp_parse_tree.h"
#include "src/language/observers.h"
#include "src/language/safe_types.h"

namespace afc::editor {
// This class is thread-safe (and does significant work in a background thread).
class BufferSyntaxParser {
 public:
  struct ParserOptions {
    std::wstring parser_name;
    std::unordered_set<wstring> typos_set;
    std::unordered_set<wstring> language_keywords;
    std::wstring symbol_characters;
    IdentifierBehavior identifier_behavior;
  };
  void UpdateParser(ParserOptions options);

  void Parse(std::unique_ptr<BufferContents> contents);

  language::NonNull<std::shared_ptr<const ParseTree>> tree() const;
  std::shared_ptr<const ParseTree> simplified_tree() const;

  std::shared_ptr<const ParseTree> current_zoomed_out_parse_tree(
      LineNumberDelta view_size, LineNumberDelta lines_size) const;

  language::Observable& ObserveTrees();

 private:
  mutable concurrent::ThreadPool thread_pool_ =
      concurrent::ThreadPool(1, nullptr);

  struct Data {
    // When the tree changes, we notify it, install a new notification, and
    // schedule in `syntax_data_` new work.
    language::NonNull<std::shared_ptr<concurrent::Notification>>
        cancel_notification =
            language::MakeNonNullShared<concurrent::Notification>();

    language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
        language::MakeNonNull(std::shared_ptr<TreeParser>(NewNullTreeParser()));

    language::NonNull<std::shared_ptr<const ParseTree>> tree =
        language::MakeNonNullShared<const ParseTree>(Range());

    // Never nullptr.
    std::shared_ptr<const ParseTree> simplified_tree =
        std::make_shared<ParseTree>(Range());

    // Caches the last parse done (by syntax_data_zoom_) for a given view size.
    struct ZoomedOutTreeData {
      // The input parse tree from which zoomed_out_parse_tree was computed.
      // This is kept so that we can detect when the parse tree has changed and
      // thus we need to start updating the zoomed_out_parse_tree (if the view
      // is still active).
      std::shared_ptr<const ParseTree> simplified_tree;
      std::shared_ptr<const ParseTree> zoomed_out_tree;
    };
    mutable std::unordered_map<LineNumberDelta, ZoomedOutTreeData>
        zoomed_out_trees;
  };

  static void ValidateInvariants(const Data& data);

  const std::shared_ptr<concurrent::Protected<
      Data, decltype(&BufferSyntaxParser::ValidateInvariants)>>
      data_ = std::make_shared<concurrent::Protected<
          Data, decltype(&BufferSyntaxParser::ValidateInvariants)>>(
          Data(), BufferSyntaxParser::ValidateInvariants);

  const std::shared_ptr<language::Observers> observers_ =
      std::make_shared<language::Observers>();
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
