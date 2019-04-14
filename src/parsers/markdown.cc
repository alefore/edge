#include "src/parsers/markdown.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer_contents.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc {
namespace editor {
namespace parsers {
namespace {

enum State {
  DEFAULT,
  SECTION_0,
  SECTION_1,
  SECTION_2,
  SECTION_3,
  SECTION_4,
  SECTION_5,
  EM,
  STRONG,
  CODE,
};

class MarkdownParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;

    std::vector<size_t> states_stack = {DEFAULT};
    std::vector<ParseTree*> trees = {root};
    for (size_t i = root->range.begin.line; i < root->range.end.line; i++) {
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(i + 1, 0), root->range.end));
      data.set_position(std::max(LineColumn(i, 0), root->range.begin));
      ParseLine(&data);
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, i);
      }
      states_stack = data.parse_results()->states_stack;
    }

    auto final_position = LineColumn(buffer.size() - 1, buffer.back()->size());
    if (final_position >= root->range.end) {
      DVLOG(5) << "Draining final states: " << states_stack.size();
      ParseData data(
          buffer, std::move(states_stack),
          std::min(LineColumn(buffer.size() + 1, 0), root->range.end));
      while (!data.parse_results()->states_stack.empty()) {
        data.PopBack();
      }
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, final_position.line);
      }
    }
  }

  void ParseLine(ParseData* result) {
    auto seek = result->seek();
    while (seek.read() == L' ') {
      seek.Once();
    }

    switch (seek.read()) {
      case L'#':
        HandleHeader(result);
        return;

      case L'*':
        HandleList(result);
        return;

      default:
        HandleNormalLine(result);
        return;
    }
  }

 private:
  void HandleNormalLine(ParseData* result) {
    auto seek = result->seek();
    while (seek.read() != L'\n') {
      switch (seek.read()) {
        case L'*':
          HandleStar(result);
          break;
        case L'`':
          HandleBackTick(result);
          break;
        default:
          seek.Once();
      }
    }
  }

  void HandleList(ParseData* result) {
    result->seek().Once();
    HandleNormalLine(result);
  }

  void HandleBackTick(ParseData* result) {
    auto seek = result->seek();
    seek.Once();
    if (result->state() == CODE) {
      result->PopBack();
    } else {
      result->Push(CODE, 1, {LineModifier::CYAN});
    }
  }

  void HandleStar(ParseData* result) {
    auto seek = result->seek();
    seek.Once();
    if (seek.read() == L'*') {
      if (result->state() == STRONG) {
        seek.Once();
        result->PopBack();
      } else {
        result->Push(STRONG, 1, {LineModifier::BOLD});
        seek.Once();
      }
    } else {
      if (result->state() == EM) {
        result->PopBack();
      } else {
        result->Push(EM, 1, {LineModifier::ITALIC});
      }
    }
  }

  void HandleHeader(ParseData* result) {
    const auto position = result->position();
    auto seek = result->seek();

    size_t depth = 0;
    while (seek.read() == L'#') {
      result->seek().Once();
      depth++;
    }
    CHECK_GT(depth, 0u);
    depth--;
    result->set_position(position);

    while (result->state() != DEFAULT &&
           (result->state() == EM || result->state() == STRONG ||
            result->state() == CODE ||
            StateToDepth(static_cast<State>(result->state())) >= depth)) {
      result->PopBack();
    }

    if (depth <= 5) {
      result->Push(DepthToState(depth), 0, {});
    }

    LineModifierSet modifiers;
    if (depth < 2) {
      if (depth == 0) {
        modifiers.insert(LineModifier::UNDERLINE);
      }
      modifiers.insert(LineModifier::BOLD);
    } else if (depth == 2) {
      modifiers.insert(LineModifier::YELLOW);
    } else if (depth == 3) {
      modifiers.insert(LineModifier::CYAN);
    }

    AdvanceLine(result, std::move(modifiers));
  }

  size_t StateToDepth(State state) {
    switch (state) {
      case SECTION_0:
        return 0;
      case SECTION_1:
        return 1;
      case SECTION_2:
        return 2;
      case SECTION_3:
        return 3;
      case SECTION_4:
        return 4;
      case SECTION_5:
        return 5;
      default:
        LOG(FATAL) << "Invalid state: " << state;
        return 0;  // Silence warning.
    }
  }

  State DepthToState(size_t depth) {
    switch (depth) {
      case 0:
        return SECTION_0;
      case 1:
        return SECTION_1;
      case 2:
        return SECTION_2;
      case 3:
        return SECTION_3;
      case 4:
        return SECTION_4;
      case 5:
        return SECTION_5;
      default:
        CHECK(false) << "Invalid depth: " << depth;
    }
  }

  void AdvanceLine(ParseData* result, LineModifierSet modifiers) {
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column, {modifiers});
  }
};

}  // namespace

std::unique_ptr<TreeParser> NewMarkdownTreeParser() {
  return std::make_unique<MarkdownParser>();
}
}  // namespace parsers
}  // namespace editor
}  // namespace afc
