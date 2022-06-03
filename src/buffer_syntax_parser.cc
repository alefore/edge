#include "src/buffer_syntax_parser.h"

#include "src/cpp_parse_tree.h"
#include "src/language/safe_types.h"
#include "src/parse_tree.h"
#include "src/parsers/diff.h"
#include "src/parsers/markdown.h"

namespace afc::editor {
using concurrent::Notification;
using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::NonNull;
using language::Observers;

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
      data.tree_parser = parsers::NewMarkdownTreeParser();
    } else {
      data.tree_parser = NewNullTreeParser();
    }
  });
}

void BufferSyntaxParser::Parse(
    NonNull<std::unique_ptr<BufferContents>> contents) {
  data_->lock([&pool = thread_pool_, data_ptr = data_, observers = observers_,
               shared_contents = NonNull<std::shared_ptr<BufferContents>>(
                   std::move(contents))](Data& data) {
    if (TreeParser::IsNull(data.tree_parser.get().get())) return;

    data.cancel_notification->Notify();
    data.cancel_notification = NonNull<std::shared_ptr<Notification>>();

    pool.RunIgnoringResult([shared_contents, parser = data.tree_parser,
                            notification = data.cancel_notification, data_ptr,
                            observers] {
      static Tracker tracker(
          L"OpenBuffer::MaybeStartUpdatingSyntaxTrees::produce");
      auto tracker_call = tracker.Call();
      VLOG(3) << "Executing parse tree update.";
      if (notification->HasBeenNotified()) return;
      NonNull<std::shared_ptr<const ParseTree>> tree =
          MakeNonNullShared<const ParseTree>(parser->FindChildren(
              shared_contents.value(), shared_contents->range()));
      data_ptr->lock([notification, tree,
                      simplified_tree = MakeNonNullShared<const ParseTree>(
                          SimplifyTree(tree.value()))](Data& data_nested) {
        if (notification->HasBeenNotified()) return;
        data_nested.tree = std::move(tree);
        data_nested.simplified_tree = std::move(simplified_tree);
      });
      observers->Notify();
    });
  });
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
                              simplified_tree = data.simplified_tree,
                              notification = data.cancel_notification, data_ptr,
                              observers]() {
        static Tracker tracker(
            L"OpenBuffer::current_zoomed_out_parse_tree::produce");
        auto tracker_call = tracker.Call();
        if (notification->HasBeenNotified()) return;

        Data::ZoomedOutTreeData output = {
            .simplified_tree = simplified_tree,
            .zoomed_out_tree = MakeNonNullShared<const ParseTree>(
                ZoomOutTree(simplified_tree.value(), lines_size, view_size))};
        data_ptr->lock([view_size, &output](Data& data) {
          if (data.simplified_tree != output.simplified_tree) {
            LOG(INFO) << "Parse tree changed in the meantime, discarding.";
            return;
          }
          LOG(INFO) << "Installing tree.";
          data.zoomed_out_trees.insert_or_assign(view_size, std::move(output));
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
