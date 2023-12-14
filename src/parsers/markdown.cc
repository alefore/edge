#include "src/parsers/markdown.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/text/line_builder.h"
#include "src/language/text/line_sequence.h"
#include "src/parse_tools.h"
#include "src/parsers/util.h"
#include "src/seek.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Substring;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;
using afc::language::text::SortedLineSequence;

namespace afc::editor::parsers {
namespace {
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

class MarkdownParser : public LineOrientedTreeParser {
 public:
  MarkdownParser(std::wstring symbol_characters, SortedLineSequence dictionary)
      : symbol_characters_(std::move(symbol_characters)),
        dictionary_(std::move(dictionary)) {
    LOG(INFO) << "Created with dictionary entries: "
              << dictionary_.lines().size();
  }

 protected:
  void ParseLine(ParseData* result) override {
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
            LazyString str = Substring(
                result->buffer().at(original_position.line).contents(),
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

  bool IsTypo(LazyString symbol) const {
    if (dictionary_.lines().range().IsEmpty()) return false;
    LineNumber line =
        dictionary_.upper_bound(LineBuilder(LowerCase(symbol)).Build());
    if (line.IsZero()) return false;

    --line;
    return LowerCase(dictionary_.lines().at(line).contents()) !=
           LowerCase(symbol);
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
  const SortedLineSequence dictionary_;
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewMarkdownTreeParser(
    std::wstring symbol_characters, SortedLineSequence dictionary) {
  return MakeNonNullUnique<MarkdownParser>(std::move(symbol_characters),
                                           std::move(dictionary));
}
}  // namespace afc::editor::parsers
