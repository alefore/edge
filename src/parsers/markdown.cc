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
  LINK_TEXT,
  LINK_URL,
};

class MarkdownParser : public TreeParser {
 public:
  ParseTree FindChildren(const BufferContents& buffer, Range range) override {
    std::vector<size_t> states_stack = {DEFAULT};
    std::vector<ParseTree> trees = {ParseTree(range)};
    range.ForEachLine([&](LineNumber i) {
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(i + LineNumberDelta(1)), range.end));
      data.set_position(std::max(LineColumn(i), range.begin));
      ParseLine(&data);
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, i);
      }
      states_stack = data.parse_results()->states_stack;
    });

    auto final_position =
        LineColumn(buffer.EndLine(), buffer.back()->EndColumn());
    if (final_position >= range.end) {
      DVLOG(5) << "Draining final states: " << states_stack.size();
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(LineNumber(0) + buffer.size() +
                                         LineNumberDelta(1)),
                              range.end));
      while (data.parse_results()->states_stack.size() > 1) {
        data.PopBack();
      }
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, final_position.line);
      }
    }
    CHECK(!trees.empty());
    return std::move(trees[0]);
  }

  void ParseLine(ParseData* result) {
    auto seek = result->seek();
    size_t spaces = 0;
    while (seek.read() == L' ') {
      spaces++;
      seek.Once();
    }

    switch (seek.read()) {
      case L'#':
        HandleHeader(result);
        return;

      case L'*':
        HandleList(spaces, result);
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
        case L'[':
          HandleOpenLink(result);
          break;
        case L']':
          HandleCloseLink(result);
          break;
        case L')':
          HandleCloseLinkUrl(result);
          break;
        default:
          seek.Once();
      }
    }
  }

  void HandleOpenLink(ParseData* result) {
    result->seek().Once();
    result->Push(LINK_TEXT, ColumnNumberDelta(), {LineModifier::CYAN});
  }

  void HandleCloseLink(ParseData* result) {
    auto seek = result->seek();
    if (result->state() != LINK_TEXT) {
      seek.Once();
      return;
    }
    result->PopBack();
    seek.Once();
    if (seek.read() == L'(') {
      seek.Once();
      result->Push(LINK_URL, ColumnNumberDelta(), {LineModifier::UNDERLINE});
    }
  }

  void HandleCloseLinkUrl(ParseData* result) {
    auto seek = result->seek();
    if (result->state() != LINK_URL) {
      seek.Once();
      return;
    }
    result->PopBack();
    seek.Once();
  }

  void HandleList(size_t spaces_prefix, ParseData* result) {
    auto original_position = result->position();
    auto seek = result->seek();
    seek.Once();
    if (seek.read() != L' ' && seek.read() != L'\n') {
      result->set_position(original_position);
      HandleNormalLine(result);
      return;
    }
    LineModifierSet modifiers;
    switch (spaces_prefix / 2) {
      case 0:
        modifiers = {LineModifier::BOLD, LineModifier::CYAN};
        break;
      case 1:
        modifiers = {LineModifier::BOLD, LineModifier::YELLOW};
        break;
      case 2:
        modifiers = {LineModifier::BOLD, LineModifier::GREEN};
        break;
      case 3:
        modifiers = {LineModifier::CYAN};
        break;
      case 4:
        modifiers = {LineModifier::YELLOW};
        break;
      case 5:
        modifiers = {LineModifier::GREEN};
        break;
    }
    result->PushAndPop(ColumnNumberDelta(1), modifiers);
    HandleNormalLine(result);
  }

  void HandleBackTick(ParseData* result) {
    auto seek = result->seek();
    seek.Once();
    if (result->state() == CODE) {
      result->PopBack();
    } else {
      result->Push(CODE, ColumnNumberDelta(1), {LineModifier::CYAN});
    }
  }

  void HandleStar(ParseData* result) {
    auto seek = result->seek();
    seek.Once();
    if (seek.read() == L'*') {
      seek.Once();
      if (result->state() == STRONG) {
        result->PopBack();
      } else if (seek.read() != L' ' && seek.read() != L'\n') {
        result->Push(STRONG, ColumnNumberDelta(2), {LineModifier::BOLD});
      }
    } else if (result->state() == EM) {
      result->PopBack();
    } else if (seek.read() != L' ' && seek.read() != L'\n') {
      result->Push(EM, ColumnNumberDelta(1), {LineModifier::ITALIC});
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

    static const auto modifiers_by_depth = new std::vector<LineModifierSet>(
        {{LineModifier::REVERSE, LineModifier::UNDERLINE},
         {LineModifier::CYAN, LineModifier::REVERSE, LineModifier::UNDERLINE},
         {LineModifier::BOLD, LineModifier::UNDERLINE},
         {LineModifier::BOLD}});

    if (depth <= 5) {
      result->Push(DepthToState(depth), ColumnNumberDelta(0), {});
    }

    AdvanceLine(result, modifiers_by_depth->at(
                            std::min(depth, modifiers_by_depth->size() - 1)));
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
        LOG(FATAL) << "Invalid depth: " << depth;
        return SECTION_0;
    }
  }

  void AdvanceLine(ParseData* result, LineModifierSet modifiers) {
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column.ToDelta(), {modifiers});
  }
};  // namespace

}  // namespace

std::unique_ptr<TreeParser> NewMarkdownTreeParser() {
  return std::make_unique<MarkdownParser>();
}
}  // namespace parsers
}  // namespace editor
}  // namespace afc
