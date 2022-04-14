#ifndef __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
#define __AFC_EDITOR_BUFFER_SYNTAX_PARSER__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/cpp_parse_tree.h"
#include "src/notification.h"
#include "src/observers.h"
#include "src/protected.h"
#include "src/thread_pool.h"

namespace afc::editor {
// This class is thread-safe (and does significant work in a background thread).
class BufferSyntaxParser : public Observable {
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

  std::shared_ptr<const ParseTree> tree() const;
  std::shared_ptr<const ParseTree> simplified_tree() const;

  std::shared_ptr<const ParseTree> current_zoomed_out_parse_tree(
      LineNumberDelta view_size, LineNumberDelta lines_size) const;

  void Add(Observers::Observer observer) override;

 private:
  mutable ThreadPool thread_pool_ = ThreadPool(1, nullptr);

  struct Data {
    // Never null. When the tree changes, we notify it, install a new
    // notification, and schedule in `syntax_data_` new work.
    std::shared_ptr<Notification> syntax_data_cancel =
        std::make_shared<Notification>();

    std::shared_ptr<TreeParser> tree_parser = NewNullTreeParser();

    // Never nullptr.
    std::shared_ptr<const ParseTree> tree =
        std::make_shared<ParseTree>(Range());

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

  const std::shared_ptr<
      Protected<Data, decltype(&BufferSyntaxParser::ValidateInvariants)>>
      data_ = std::make_shared<
          Protected<Data, decltype(&BufferSyntaxParser::ValidateInvariants)>>(
          Data(), BufferSyntaxParser::ValidateInvariants);

  const std::shared_ptr<Observers> observers_ = std::make_shared<Observers>();
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
