#include "src/buffer_syntax_parser.h"

#include "src/cpp_parse_tree.h"
#include "src/language/safe_types.h"
#include "src/parse_tree.h"
#include "src/parsers/csv.h"
#include "src/parsers/diff.h"
#include "src/parsers/markdown.h"

namespace afc::editor {
using futures::DeleteNotification;
using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Observers;
using language::text::LineColumn;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::Range;

void BufferSyntaxParser::UpdateParser(ParserOptions options) {
  data_->lock([&options](Data& data) {
    if (options.parser_name == L"text") {
      data.tree_parser = NewLineTreeParser(NewWordsTreeParser(
          options.symbol_characters, options.typos_set, NewNullTreeParser()));
    } else if (options.parser_name == L"cpp") {
      data.tree_parser =
          NewCppTreeParser(options.language_keywords, options.typos_set,
                           options.identifier_behavior);
    } else if (options.parser_name == L"diff") {
      data.tree_parser = parsers::NewDiffTreeParser();
    } else if (options.parser_name == L"md") {
      data.tree_parser = parsers::NewMarkdownTreeParser(
          options.symbol_characters, options.dictionary);
    } else if (options.parser_name == L"csv") {
      data.tree_parser = parsers::NewCsvTreeParser();
    } else {
      data.tree_parser = NewNullTreeParser();
    }
  });
}

std::set<language::text::Range> BufferSyntaxParser::GetRangesForToken(
    LineColumn line_column, Range relevant_range) {
  std::set<language::text::Range> output;
  data_->lock([&](Data& data) {
    DVLOG(5) << "Get ranges for: " << line_column
             << ", relevant range: " << relevant_range;

#pragma GCC diagnostic push
// The compiler doesn't seem to understand that the `route` is just computed in
// order to find `tree`, but that nothing in `tree` refers to the route. This
// code is safe.
#pragma GCC diagnostic ignored "-Wdangling-reference"
    const ParseTree& tree = FollowRoute(
        data.tree.value(), FindRouteToPosition(data.tree.value(), line_column));
#pragma GCC diagnostic pop
    if (!tree.range().Contains(line_column) || !tree.children().empty()) return;

    std::unordered_map<language::text::Range, size_t>::iterator it_token =
        data.token_id.find(tree.range());
    if (it_token == data.token_id.end()) return;
    CHECK_LT(it_token->second, data.token_partition.size());
    DVLOG(6) << "Found token partition set: " << it_token->second;
    const std::set<language::text::Range>& token_set =
        data.token_partition[it_token->second];
    auto it = token_set.lower_bound(relevant_range);
    while (it != token_set.begin() &&
           std::prev(it)->end() > relevant_range.begin())
      --it;
    while (it != token_set.end() && it->begin() <= relevant_range.end()) {
      output.insert(*it);
      ++it;
    }
  });
  DVLOG(4) << "Returning ranges: " << output.size();
  return output;
}

namespace {
std::wstring GetSymbol(const Range& range, const LineSequence& contents) {
  return contents.at(range.begin().line)
      .Substring(range.begin().column,
                 range.end().column - range.begin().column)
      .ToString();
}

void PrepareTokenPartition(
    NonNull<const ParseTree*> tree, const LineSequence& contents,
    std::unordered_map<language::text::Range, size_t>& output_token_id,
    std::vector<std::set<language::text::Range>>& output_token_partition) {
  std::vector<NonNull<const ParseTree*>> trees = {tree};
  std::unordered_map<std::wstring, size_t> contents_to_id;
  while (!trees.empty()) {
    NonNull<const ParseTree*> head = trees.back();
    trees.pop_back();
    const std::vector<ParseTree>& children = head.value().children();
    if (children.empty() &&
        head->range().begin().line == head->range().end().line) {
      auto insert_results = contents_to_id.insert(
          {GetSymbol(head->range(), contents), output_token_partition.size()});
      if (insert_results.second) {
        output_token_partition.push_back({});
      }
      size_t id = insert_results.first->second;
      output_token_id.insert({head->range(), id});
      output_token_partition[id].insert(head->range());
    }
    for (const ParseTree& c : children)
      trees.push_back(NonNull<const ParseTree*>::AddressOf(c));
  }
}
}  // namespace

void BufferSyntaxParser::Parse(const LineSequence contents) {
  parse_channel_.Push(contents);
}

void BufferSyntaxParser::ParseInternal(const LineSequence contents) {
  language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
      data_->lock([](const Data& data) { return data.tree_parser; });
  if (TreeParser::IsNull(tree_parser.get().get())) return;

  TRACK_OPERATION(BufferSyntaxParser_ParseInternal_produce);
  VLOG(3) << "Executing parse tree update.";

  NonNull<std::shared_ptr<const ParseTree>> tree =
      MakeNonNullShared<const ParseTree>(
          tree_parser->FindChildren(contents, contents.range()));

  std::unordered_map<language::text::Range, size_t> token_id;
  std::vector<std::set<language::text::Range>> token_partition;
  PrepareTokenPartition(tree.get(), contents, token_id, token_partition);
  DVLOG(5) << "Generated partitions: [entries: " << token_id.size()
           << "][sets: " << token_partition.size() << "]";
  data_->lock([tree, token_id = std::move(token_id),
               token_partition = std::move(token_partition),
               simplified_tree = MakeNonNullShared<const ParseTree>(
                   SimplifyTree(tree.value()))](Data& data_nested) mutable {
    data_nested.tree = std::move(tree);
    data_nested.token_id = std::move(token_id);
    data_nested.token_partition = std::move(token_partition);
    data_nested.simplified_tree = std::move(simplified_tree);
  });
  observers_->Notify();
}

NonNull<std::shared_ptr<const ParseTree>> BufferSyntaxParser::tree() const {
  return data_->lock()->tree;
}

NonNull<std::shared_ptr<const ParseTree>> BufferSyntaxParser::simplified_tree()
    const {
  return data_->lock()->simplified_tree;
}

NonNull<std::shared_ptr<const ParseTree>>
BufferSyntaxParser::current_zoomed_out_parse_tree(
    LineNumberDelta view_size, LineNumberDelta lines_size) const {
  return data_->lock([view_size, lines_size, data_ptr = data_,
                      &pool = thread_pool_,
                      observers = observers_](Data& data) {
    auto it = data.zoomed_out_trees.find(view_size);
    if (it == data.zoomed_out_trees.end() ||
        it->second.simplified_tree != data.simplified_tree) {
      pool.RunIgnoringResult([view_size, lines_size,
                              simplified_tree = data.simplified_tree, data_ptr,
                              observers]() {
        TRACK_OPERATION(
            BufferSyntaxParser_current_zoomed_out_parse_tree_produce);

        Data::ZoomedOutTreeData output = {
            .simplified_tree = simplified_tree,
            .zoomed_out_tree = MakeNonNullShared<const ParseTree>(
                ZoomOutTree(simplified_tree.value(), lines_size, view_size))};
        data_ptr->lock([view_size, &output](Data& data_nested) {
          if (data_nested.simplified_tree != output.simplified_tree) {
            LOG(INFO) << "Parse tree changed in the meantime, discarding.";
            return;
          }
          LOG(INFO) << "Installing tree.";
          data_nested.zoomed_out_trees.insert_or_assign(view_size,
                                                        std::move(output));
        });
        observers->Notify();
      });
    }

    // We don't check if it's still current: we prefer returning a stale tree
    // over an empty tree. The empty tree would just cause flickering as the
    // user is typing; the stale tree is almost always correct (and, when it
    // isn't, it'll be refreshed very shortly).
    return it != data.zoomed_out_trees.end()
               ? it->second.zoomed_out_tree
               : MakeNonNullShared<const ParseTree>(Range());
  });
}

language::Observable& BufferSyntaxParser::ObserveTrees() {
  return observers_.value();
}
}  // namespace afc::editor
