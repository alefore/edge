#include "src/parsers/diff.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer_contents.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc {
namespace editor {
namespace parsers {
namespace {

enum State { DEFAULT, HEADERS, SECTION, CONTENTS };

class DiffParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->Reset();

    std::vector<size_t> states_stack = {DEFAULT};
    std::vector<ParseTree*> trees = {root};
    for (LineNumber i = root->range().begin.line; i < root->range().end.line;
         ++i) {
      ParseData data(
          buffer, std::move(states_stack),
          std::min(LineColumn(i + LineNumberDelta(1)), root->range().end));
      data.set_position(std::max(LineColumn(i), root->range().begin));
      ParseLine(&data);
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, i);
      }
      states_stack = data.parse_results()->states_stack;
    }

    auto final_position =
        LineColumn(buffer.EndLine(), buffer.back()->EndColumn());
    if (final_position >= root->range().end) {
      DVLOG(5) << "Draining final states: " << states_stack.size();
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(LineNumber(0) + buffer.size() +
                                         LineNumberDelta(1)),
                              root->range().end));
      while (!data.parse_results()->states_stack.empty()) {
        data.PopBack();
      }
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, final_position.line);
      }
    }
  }

  void ParseLine(ParseData* result) {
    switch (result->seek().read()) {
      case L'\n':
      case L' ':
        InContents(result, {});
        return;

      case L'+':
        if (result->state() == HEADERS) {
          AdvanceLine(result, {LineModifier::BOLD});
          return;
        }
        // Fall through.
      case L'>':
        InContents(result, {LineModifier::GREEN});
        return;

      case L'-':
        if (result->state() == HEADERS) {
          AdvanceLine(result, {LineModifier::BOLD});
          return;
        }
        // Fall through.
      case L'<':
        InContents(result, {LineModifier::RED});
        return;

      case L'@':
        if (result->state() == CONTENTS) {
          result->PopBack();
        }
        if (result->state() == SECTION) {
          result->PopBack();
        }
        result->Push(SECTION, ColumnNumberDelta(), {});
        AdvanceLine(result, {LineModifier::CYAN});
        return;

      default:
        if (result->state() != HEADERS) {
          if (result->state() == CONTENTS) {
            result->PopBack();
          }
          if (result->state() == SECTION) {
            result->PopBack();
          }
          if (result->state() == HEADERS) {
            result->PopBack();
          }
          result->Push(HEADERS, ColumnNumberDelta(), {});
        }
        AdvanceLine(result, {LineModifier::BOLD});
        return;
    }
  }

 private:
  void AdvanceLine(ParseData* result, LineModifierSet modifiers) {
    auto original_column = result->position().column;
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column - original_column,
                       {modifiers});
  }

  void InContents(ParseData* result, LineModifierSet modifiers) {
    if (result->state() != CONTENTS) {
      result->Push(CONTENTS, ColumnNumberDelta(), {});
    }
    AdvanceLine(result, modifiers);
  }
};

}  // namespace

std::unique_ptr<TreeParser> NewDiffTreeParser() {
  return std::make_unique<DiffParser>();
}
}  // namespace parsers
}  // namespace editor
}  // namespace afc
