#include "src/parsers/markdown.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/text/line_sequence.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc::editor::parsers {
namespace {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::lazy_string::Substring;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::Range;

using ::operator<<;

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
  LINK,
  LINK_TEXT,
  LINK_URL,
  SYMBOL,
};

class MarkdownParser : public TreeParser {
 public:
  MarkdownParser(std::wstring symbol_characters, LineSequence dictionary)
      : symbol_characters_(std::move(symbol_characters)),
        dictionary_(std::move(dictionary)) {
    LOG(INFO) << "Created with dictionary entries: " << dictionary_.size();
  }

  ParseTree FindChildren(const LineSequence& buffer, Range range) override {
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
          if (IsSymbol(seek.read())) {
            LineColumn original_position = result->position();
            while (!seek.AtRangeEnd() && IsSymbol(seek.read())) seek.Once();
            ColumnNumberDelta length =
                result->position().column - original_position.column;
            NonNull<std::shared_ptr<LazyString>> str = Substring(
                result->buffer().at(original_position.line)->contents(),
                original_position.column, length);
            result->PushAndPop(length, IsTypo(str)
                                           ? LineModifierSet{LineModifier::kRed}
                                           : LineModifierSet{});
          } else {
            seek.Once();
          }
      }
    }
  }

  bool IsSymbol(int c) const {
    return symbol_characters_.find(c) != symbol_characters_.npos;
  }

  bool IsTypo(NonNull<std::shared_ptr<LazyString>> symbol) const {
    if (dictionary_.range().IsEmpty()) return false;
    LineNumber line = dictionary_.upper_bound(
        MakeNonNullShared<const Line>(LineBuilder(LowerCase(symbol)).Build()),
        [](const NonNull<std::shared_ptr<const Line>>& a,
           const NonNull<std::shared_ptr<const Line>>& b) {
          return a->ToString() < b->ToString();
        });
    if (line.IsZero()) return false;

    --line;
    return LowerCase(dictionary_.at(line)->contents()).value() !=
           LowerCase(symbol).value();
  }

  void HandleOpenLink(ParseData* result) {
    result->Push(LINK, ColumnNumberDelta(), {}, {ParseTreeProperty::Link()});
    result->seek().Once();
    result->Push(LINK_TEXT, ColumnNumberDelta(), {LineModifier::kCyan}, {});
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
      result->Push(LINK_URL, ColumnNumberDelta(), {LineModifier::kUnderline},
                   {ParseTreeProperty::LinkTarget()});
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
    if (result->state() != LINK) {
      return;
    }
    result->PopBack();
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
        modifiers = {LineModifier::kBold, LineModifier::kCyan};
        break;
      case 1:
        modifiers = {LineModifier::kBold, LineModifier::kYellow};
        break;
      case 2:
        modifiers = {LineModifier::kBold, LineModifier::kGreen};
        break;
      case 3:
        modifiers = {LineModifier::kCyan};
        break;
      case 4:
        modifiers = {LineModifier::kYellow};
        break;
      case 5:
        modifiers = {LineModifier::kGreen};
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
      result->Push(CODE, ColumnNumberDelta(1), {LineModifier::kCyan}, {});
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
        result->Push(STRONG, ColumnNumberDelta(2), {LineModifier::kBold}, {});
      }
    } else if (result->state() == EM) {
      result->PopBack();
    } else if (seek.read() != L' ' && seek.read() != L'\n') {
      result->Push(EM, ColumnNumberDelta(1), {LineModifier::kItalic}, {});
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
           StateToDepth(static_cast<State>(result->state())).value_or(depth) >=
               depth) {
      result->PopBack();
    }

    static const auto modifiers_by_depth = new std::vector<LineModifierSet>(
        {{LineModifier::kReverse, LineModifier::kUnderline},
         {LineModifier::kCyan, LineModifier::kReverse,
          LineModifier::kUnderline},
         {LineModifier::kBold, LineModifier::kUnderline},
         {LineModifier::kBold}});

    if (depth <= 5) {
      result->Push(DepthToState(depth), ColumnNumberDelta(0), {}, {});
    }

    AdvanceLine(result, modifiers_by_depth->at(
                            std::min(depth, modifiers_by_depth->size() - 1)));
  }

  std::optional<size_t> StateToDepth(State state) {
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
        return std::nullopt;
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

  const std::wstring symbol_characters_;
  const LineSequence dictionary_;
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewMarkdownTreeParser(
    std::wstring symbol_characters, LineSequence dictionary) {
  return MakeNonNullUnique<MarkdownParser>(std::move(symbol_characters),
                                           std::move(dictionary));
}
}  // namespace afc::editor::parsers
