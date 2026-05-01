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
#include "src/log_model.h"
#include "src/parse_tree.h"

namespace afc::editor {
// This class is thread-safe (and does significant work in a background thread).
class BufferSyntaxParser {
 public:
  struct ParserOptions {
    std::optional<ParserId> parser_name;
    std::unordered_set<language::lazy_string::NonEmptySingleLine> typos_set;
    std::unordered_set<language::lazy_string::NonEmptySingleLine>
        language_keywords;
    language::lazy_string::LazyString symbol_characters;
    IdentifierBehavior identifier_behavior;
    language::text::SortedLineSequence dictionary;
    std::optional<LogModel> log_model;
    std::optional<LogTypeName> log_type_name;
    std::optional<LogViewName> log_view_name;
  };
  void UpdateParser(ParserOptions options);

  // Part of the buffer that is visible. This can be used by parsers that have
  // BoundaryScope::Line to avoid parsing the entire file.
  struct View {
    // First line that is visible.
    language::text::LineNumber start;
    // Total number of lines visible.
    language::text::LineNumberDelta size;
  };
  // May be empty if we don't know yet the views.
  using Views = std::vector<View>;

  struct ParseInput {
    language::text::LineSequence contents;
    Views views;
  };
  void Parse(ParseInput input) const;

  language::NonNull<std::shared_ptr<const ParseTree>> tree(ParseInput) const;
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
  void ParseInternal(ParseInput input);

  mutable concurrent::ThreadPool thread_pool_ = concurrent::ThreadPool(
      language::lazy_string::LazyString{L"BufferSyntaxParser"}, 1);
  mutable concurrent::ChannelLast<ParseInput> parse_channel_ =
      concurrent::ChannelLast<ParseInput>(
          std::bind_front(
              &concurrent::ThreadPool::RunIgnoringResult<std::function<void()>>,
              &thread_pool_),
          std::bind_front(&BufferSyntaxParser::ParseInternal, this));

  struct Data {
    language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
        NewNullTreeParser();

    ParseInput last_parse_input_scheduled;

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

std::ostream& operator<<(std::ostream& os, const BufferSyntaxParser::View&);
std::ostream& operator<<(std::ostream& os,
                         const BufferSyntaxParser::ParseInput&);
bool operator==(const BufferSyntaxParser::View&,
                const BufferSyntaxParser::View&);
bool operator==(const BufferSyntaxParser::ParseInput&,
                const BufferSyntaxParser::ParseInput&);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_SYNTAX_PARSER__
