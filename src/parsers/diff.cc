#include "src/parsers/diff.h"

#include <algorithm>

#include <glog/logging.h>

#include "src/buffer_contents.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc {
namespace editor {
namespace parsers {
namespace {

enum State {
  DEFAULT,
};

class DiffParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;

    // TODO: Does this actually clean up expired references? Probably not?
    cache_.erase(std::weak_ptr<LazyString>());

    std::vector<size_t> states_stack = {DEFAULT};
    std::vector<ParseTree*> trees = {root};
    for (size_t i = root->range.begin.line; i < root->range.end.line; i++) {
      auto insert_results = cache_[buffer.at(i)->contents()].insert(
          {states_stack, ParseResults()});
      if (insert_results.second) {
        ParseData data(buffer, std::move(states_stack),
                       std::min(LineColumn(i + 1, 0), root->range.end));
        data.set_position(std::max(LineColumn(i, 0), root->range.begin));
        ParseLine(&data);
        insert_results.first->second = *data.parse_results();
      }
      for (auto& action : insert_results.first->second.actions) {
        action.Execute(&trees, i);
      }
      states_stack = insert_results.first->second.states_stack;
    }
  }

  void ParseLine(ParseData* result) {
    Seek seek = result->seek();
    auto original_column = result->position().column;
    auto c = seek.read();
    result->seek().ToEndOfLine();
    switch (c) {
      case L'\n':
      case L' ':
        return;

      case L'+':
      case L'>':
        result->PushAndPop(result->position().column - original_column,
                           {LineModifier::GREEN});
        return;

      case L'-':
      case L'<':
        result->PushAndPop(result->position().column - original_column,
                           {LineModifier::RED});
        return;

      case L'@':
        result->PushAndPop(result->position().column - original_column,
                           {LineModifier::CYAN});
        return;

      default:
        result->PushAndPop(result->position().column - original_column,
                           {LineModifier::BOLD});
        return;
    }
  }

 private:
  std::map<std::weak_ptr<LazyString>,
           std::map<std::vector<size_t>, ParseResults>,
           std::owner_less<std::weak_ptr<LazyString>>>
      cache_;
};

}  // namespace

std::unique_ptr<TreeParser> NewDiffTreeParser() {
  return std::make_unique<DiffParser>();
}
}  // namespace parsers
}  // namespace editor
}  // namespace afc
