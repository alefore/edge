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
#include "src/language/text/sorted_line_sequence.h"
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
    language::text::SortedLineSequence dictionary;
  };
  void UpdateParser(ParserOptions options);

  void Parse(language::text::LineSequence contents);

  language::NonNull<std::shared_ptr<const ParseTree>> tree() const;
  language::NonNull<std::shared_ptr<const ParseTree>> simplified_tree() const;

  language::NonNull<std::shared_ptr<const ParseTree>>
  current_zoomed_out_parse_tree(
      language::text::LineNumberDelta view_size,
      language::text::LineNumberDelta lines_size) const;

  language::Observable& ObserveTrees();

  // Based on `Data::tokens`, returns a list of all the ranges in the tree that
  // intersect `relevant_range` and that contain exactly the token that's in
  // `line_column`.
  std::set<language::text::Range> GetRangesForToken(
      language::text::LineColumn line_column,
      language::text::Range relevant_range);

 private:
  void ParseInternal(language::text::LineSequence contents);

  mutable concurrent::ThreadPool thread_pool_ = concurrent::ThreadPool(1);
  concurrent::ChannelLast<language::text::LineSequence> parse_channel_ =
      concurrent::ChannelLast<language::text::LineSequence>(
          std::bind_front(
              &concurrent::ThreadPool::RunIgnoringResult<std::function<void()>>,
              &thread_pool_),
          std::bind_front(&BufferSyntaxParser::ParseInternal, this));

  struct Data {
    language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
        NewNullTreeParser();

    language::NonNull<std::shared_ptr<const ParseTree>> tree =
        language::MakeNonNullShared<const ParseTree>(language::text::Range());

    // We partition every leaf in `tree`. Each set in the partition contains all
    // the leafs that have the same content. The value is the ID of a given
    // partition and indexed `token_partition`.
    std::unordered_map<language::text::Range, size_t> token_id;
    // Stores the partition of tokens based on their content. The index are the
    // values in `token_id`.
    std::vector<std::set<language::text::Range>> token_partition;

    language::NonNull<std::shared_ptr<const ParseTree>> simplified_tree =
        language::MakeNonNullShared<const ParseTree>(language::text::Range());

    // Caches the last parse done for a given view size.
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
