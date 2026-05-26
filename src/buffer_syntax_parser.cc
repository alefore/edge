#include "src/buffer_syntax_parser.h"

#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/safe_types.h"
#include "src/parse_tree.h"
#include "src/parsers/cpp.h"
#include "src/parsers/css.h"
#include "src/parsers/csv.h"
#include "src/parsers/diff.h"
#include "src/parsers/log.h"
#include "src/parsers/markdown.h"
#include "src/parsers/py.h"

namespace gc = afc::language::gc;

using afc::futures::DeleteNotification;
using afc::infrastructure::Path;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Observers;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;
using afc::language::text::SortedLineSequence;

namespace afc::editor {
futures::ValueOrError<SortedLineSequence> LoadDictionary(
    EditorState& editor, LazyString dictionary_path_str) {
  DECLARE_OR_RETURN(Path dictionary_path, Path::New(dictionary_path_str));
  // TODO(P0, 2026-04-29): I think loading the dictionary is broken: it should
  // apply editor.edge_path!
  return OpenFileIfFound(
             OpenFileOptions{
                 .editor_state = editor,
                 .path = std::move(dictionary_path),
                 .insertion_type = BuffersList::AddBufferType::Ignore})
      .Transform([](gc::Root<OpenBuffer> dictionary_root)
                     -> futures::ValueOrError<gc::Root<OpenBuffer>> {
        return dictionary_root->WaitForEndOfFile();
      })
      .Transform([](gc::Root<OpenBuffer> dictionary_root)
                     -> futures::ValueOrError<SortedLineSequence> {
        return dictionary_root->editor().thread_pool().Run(
            [contents = dictionary_root->contents().snapshot()] {
              return SortedLineSequence(contents);
            });
      });
}

void BufferSyntaxParser::UpdateParser(ParserOptions options) {
  (options.parser_name == ParserId::Markdown()
       ? LoadDictionary(options.editor, options.dictionary_path)
             .ConsumeErrors(
                 [](Error) { return SortedLineSequence{LineSequence{}}; })
       : futures::Value<SortedLineSequence>{SortedLineSequence{LineSequence{}}})
      .Transform([options,
                  protected_data = data_](SortedLineSequence dictionary) {
        protected_data->lock([&options, &dictionary](Data& data) {
          data.tree_parser_screens_buffer = options.tree_parser_screens_buffer;
          data.tree_parser = NewNullTreeParser();
          if (options.parser_name == ParserId::Text()) {
            data.tree_parser = NewLineTreeParser(
                NewWordsTreeParser(options.symbol_characters, options.typos_set,
                                   NewNullTreeParser()));
          } else if (options.parser_name == ParserId::Cpp() ||
                     options.parser_name == ParserId::Java() ||
                     options.parser_name == ParserId::JavaScript()) {
            data.tree_parser = parsers::NewCppTreeParser(
                options.parser_name.value(), options.language_keywords,
                options.typos_set, options.identifier_behavior);
          } else if (options.parser_name == ParserId::Diff()) {
            data.tree_parser = parsers::NewDiffTreeParser();
          } else if (options.parser_name == ParserId::Markdown()) {
            data.tree_parser = parsers::NewMarkdownTreeParser(
                options.symbol_characters, dictionary);
          } else if (options.parser_name == ParserId::Csv()) {
            data.tree_parser = parsers::NewCsvTreeParser();
          } else if (options.parser_name == ParserId::Css()) {
            data.tree_parser =
                parsers::NewCssTreeParser(options.parser_name.value());
          } else if (options.parser_name == ParserId::Py()) {
            data.tree_parser = parsers::NewPyTreeParser(
                options.language_keywords, options.typos_set,
                options.identifier_behavior);
          } else if (options.parser_name == ParserId::Log()) {
            if (options.log_model.has_value() &&
                options.log_type_name.has_value()) {
              const auto& log_types = options.log_model->log_types;
              if (auto it_type = log_types.find(options.log_type_name.value());
                  it_type != log_types.end()) {
                const auto& views = options.log_model->views;
                if (auto it_view = views.find(options.log_view_name.value());
                    it_view != views.end()) {
                  data.tree_parser = parsers::NewLogTreeParser(it_type->second,
                                                               it_view->second);
                } else {
                  LOG(INFO) << "Unable to find log view: "
                            << options.log_view_name.value();
                }
              } else {
                LOG(INFO) << "Unable to find log type: "
                          << options.log_type_name.value();
              }
            } else {
              LOG(INFO) << "Log model or log type name are missing.";
            }
          }
        });
        return EmptyValue{};
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
SingleLine GetSymbol(const Range& range, const LineSequence& contents) {
  return contents.at(range.begin().line)
      ->Substring(range.begin().column,
                  range.end().column - range.begin().column);
}

void PrepareTokenPartition(
    NonNull<const ParseTree*> tree, const LineSequence& contents,
    std::unordered_map<language::text::Range, size_t>& output_token_id,
    std::vector<std::set<language::text::Range>>& output_token_partition) {
  std::vector<NonNull<const ParseTree*>> trees = {tree};
  std::unordered_map<SingleLine, size_t> contents_to_id;
  while (!trees.empty()) {
    NonNull<const ParseTree*> head = trees.back();
    trees.pop_back();
    const std::vector<ParseTree>& children = head.value().children();
    if (children.empty() &&
        head->range().begin().line == head->range().end().line &&
        !head->range().empty()) {
      auto insert_results = contents_to_id.insert(
          {GetSymbol(head->range(), contents), output_token_partition.size()});
      if (insert_results.second) output_token_partition.push_back({});
      size_t id = insert_results.first->second;
      output_token_id.insert({head->range(), id});
      output_token_partition[id].insert(head->range());
    }
    for (const ParseTree& c : children)
      trees.push_back(NonNull<const ParseTree*>::AddressOf(c));
  }
}
}  // namespace

void BufferSyntaxParser::Parse(ParseInput input) const {
  parse_channel_.Push(std::move(input));
}

void BufferSyntaxParser::ParseInternal(ParseInput input) {
  size_t tree_parser_screens_buffer;
  language::NonNull<std::shared_ptr<TreeParser>> tree_parser =
      data_->lock([&](const Data& data) {
        tree_parser_screens_buffer = data.tree_parser_screens_buffer;
        return data.tree_parser;
      });
  if (TreeParser::IsNull(tree_parser.get().get())) return;

  if (tree_parser->state_boundary() == TreeParser::StateBoundary::Line &&
      input.views.empty()) {
    VLOG(4) << "TreeParser has StateBoundary::Line but view is not yet known. "
               "Skipping update as an optimization "
               "(will update once view becomes known).";
    return;
  }

  TRACK_OPERATION(BufferSyntaxParser_ParseInternal_produce);
  VLOG(3) << "Executing parse tree update: " << input;

  Range range = std::invoke([&] {
    switch (tree_parser->state_boundary()) {
      using enum TreeParser::StateBoundary;
      case Line: {
        const View& view = input.views[0];
        const LineNumberDelta margin = view.size * tree_parser_screens_buffer;
        VLOG(4) << "Margin: " << margin << " (view.size: " << view.size << ")";
        return input.contents.range().Intersection(Range(
            LineColumn(view.start - std::min(view.start.ToDelta(), margin)),
            LineColumn(std::min(view.start + view.size + margin,
                                input.contents.EndLine()))));
      }

      case AllContents:
        return input.contents.range();
    }
    LOG(FATAL) << "Invalid state boundary.";
    std::unreachable();
  });
  NonNull<std::shared_ptr<const ParseTree>> tree =
      MakeNonNullShared<const ParseTree>(
          tree_parser->FindChildren(input.contents, range));

  std::unordered_map<language::text::Range, size_t> token_id;
  std::vector<std::set<language::text::Range>> token_partition;
  PrepareTokenPartition(tree.get(), input.contents, token_id, token_partition);
  DVLOG(5) << "Generated partitions: [entries: " << token_id.size()
           << "][sets: " << token_partition.size() << "]";
  auto simplified_tree = MakeNonNullShared<const ParseTree>(std::invoke([&] {
    switch (tree_parser->state_boundary()) {
      case TreeParser::StateBoundary::AllContents:
        return SimplifyTree(tree.value());
      case TreeParser::StateBoundary::Line:
        return ParseTree(tree->range());
    }
    LOG(FATAL) << "Invalid tree parser state boundary.";
    std::unreachable();
  }));
  data_->lock([input = std::move(input), tree, token_id = std::move(token_id),
               token_partition = std::move(token_partition),
               simplified_tree =
                   std::move(simplified_tree)](Data& data_nested) mutable {
    data_nested.tree = std::move(tree);
    data_nested.token_id = std::move(token_id);
    data_nested.token_partition = std::move(token_partition);
    data_nested.simplified_tree = std::move(simplified_tree);
  });
  observers_->Notify();
}

NonNull<std::shared_ptr<const ParseTree>> BufferSyntaxParser::tree(
    ParseInput input) const {
  NonNull<std::shared_ptr<const ParseTree>> output;
  bool trigger_update = data_->lock([&](Data& data) {
    output = data.tree;
    if (data.last_parse_input_scheduled == input) return false;
    data.last_parse_input_scheduled = input;
    return true;
  });
  if (trigger_update) Parse(input);
  return output;
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
          LOG(INFO) << "Installing tree: " << view_size;
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

std::ostream& operator<<(std::ostream& os, const BufferSyntaxParser::View& v) {
  os << "[view: " << v.start << " " << v.size << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const BufferSyntaxParser::ParseInput& i) {
  os << "[input size:" << i.contents.size();
  for (const auto& v : i.views) os << " " << v;
  os << "]";
  return os;
}

bool operator==(const BufferSyntaxParser::View& a,
                const BufferSyntaxParser::View& b) {
  return a.start == b.start && a.size == b.size;
}

bool operator==(const BufferSyntaxParser::ParseInput& a,
                const BufferSyntaxParser::ParseInput& b) {
  return a.contents == b.contents && a.views == b.views;
}
}  // namespace afc::editor
