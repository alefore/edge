#include "src/transformation/reach_query.h"

#include "src/buffer.h"
#include "src/buffer_display_data.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/screen/visual_overlay.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::VisualOverlayKey;
using afc::infrastructure::screen::VisualOverlayMap;
using afc::infrastructure::screen::VisualOverlayPriority;
using afc::language::NonNull;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;

namespace afc::editor::transformation {
using ::operator<<;
namespace {
static const ColumnNumberDelta kQueryLength = ColumnNumberDelta(2);

std::vector<LineColumn> FindPositions(const SingleLine& query,
                                      const OpenBuffer& buffer) {
  std::vector<LineColumn> output;
  LineColumn view_start = buffer.Read(buffer_variables::view_start);
  std::optional<LineColumnDelta> view_size =
      buffer.display_data().view_size().Get();
  if (view_size == std::nullopt) return output;
  LineNumber end_line = view_start.line + view_size->line;
  while (view_start.line < end_line && view_start.line <= buffer.EndLine()) {
    Line line = buffer.contents().snapshot().at(view_start.line);
    while (view_start.column +
               std::max(kQueryLength + ColumnNumberDelta(1), query.size()) <=
           line.EndColumn()) {
      bool match = true;
      for (ColumnNumberDelta i; i < query.size() && match; ++i) {
        match = std::tolower(
                    static_cast<wchar_t>(line.get(view_start.column + i))) ==
                std::tolower(query.get(ColumnNumber{} + i));
      }
      if (match) output.push_back(view_start);
      ++view_start.column;
    }
    view_start = LineColumn(LineNumber(view_start.line + LineNumberDelta(1)));
  }
  return output;
}

using Identifier = wchar_t;

// If the match is "abc", the outter key is `b` and the inner key is `c` (or
// a synthetic identifier).
using PositionIdentifierMap =
    std::map<Identifier, std::map<Identifier, LineColumn>>;

bool FindSyntheticIdentifier(LineColumn position, const LineSequence& contents,
                             PositionIdentifierMap& output) {
  static const std::wstring kIdentifiers =
      L"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const Line& line = contents.at(position.line);
  const Identifier first_identifier =
      std::tolower(line.get(position.column + ColumnNumberDelta(1)));
  const Identifier desired_identifier =
      line.get(position.column + ColumnNumberDelta(2));
  size_t start_position = kIdentifiers.find_first_of(desired_identifier);
  for (size_t i = 0; i < kIdentifiers.size(); i++) {
    Identifier candidate =
        kIdentifiers[(start_position + i) % kIdentifiers.size()];
    if (output[first_identifier].insert({candidate, position}).second) {
      VLOG(5) << "Found synthetic identifier: " << static_cast<int>(candidate)
              << ": " << position;
      return true;
    }
  }
  return false;
}

PositionIdentifierMap FindIdentifiers(std::vector<LineColumn> matches,
                                      const LineSequence& contents) {
  PositionIdentifierMap output;
  std::vector<LineColumn> pending;

  // Copy all elements in matches to `output` or `pending`. `pending` will
  // contain the ones where their desired identifier was already used by a
  // different position. This lets us bias towards reducing the number of
  // "invented" identifiers.
  for (LineColumn position : matches) {
    Line line = contents.at(position.line);
    const Identifier desired_identifier =
        line.get(position.column + kQueryLength);
    if (!output[std::tolower(line.get(position.column + ColumnNumberDelta(1)))]
             .insert({desired_identifier, position})
             .second)
      pending.push_back(position);
  }

  // There's a ~square-complexity algorithm here, when `output` gets full. We
  // could remember external identifiers for which this has happened and give up
  // on them.
  for (LineColumn position : pending) {
    FindSyntheticIdentifier(position, contents, output);
  }
  return output;
}
}  // namespace

ReachQueryTransformation::ReachQueryTransformation(SingleLine query)
    : query_(std::move(query)) {}

std::wstring ReachQueryTransformation::Serialize() const {
  return L"ReachQueryTransformation()";
}

futures::Value<CompositeTransformation::Output> GoTo(
    std::optional<LineColumn> position) {
  // TODO(easy, 2023-02-13): This can be simplified significantly.
  CompositeTransformation::Output output(VisualOverlay{VisualOverlayMap{}});
  VisitPointer(
      position, [&](LineColumn value) { output.Push(SetPosition(value)); },
      [] {});
  return futures::Past(std::move(output));
}

futures::Value<CompositeTransformation::Output> ReachQueryTransformation::Apply(
    CompositeTransformation::Input input) const {
  if (query_.empty() || query_.size() > kQueryLength + ColumnNumberDelta(1))
    return futures::Past(Output{});

  PositionIdentifierMap matches = FindIdentifiers(
      FindPositions(
          query_.SubstringWithRangeChecks(ColumnNumber{0}, kQueryLength),
          input.buffer),
      input.buffer.contents().snapshot());

  LOG(INFO) << "Found matches: " << matches.size();

  if (matches.empty()) return futures::Past(Output{});

  if (matches.size() == 1 && matches.begin()->second.size() == 1) {
    return GoTo(matches.begin()->second.begin()->second);
  }

  if (ColumnNumberDelta(query_.size()) == kQueryLength + ColumnNumberDelta(1)) {
    std::map<Identifier, LineColumn>& dictionary =
        matches[std::tolower(query_.get(ColumnNumber{1}))];
    LOG(INFO) << "Query is done, possibilities: " << dictionary.size();
    Identifier id = query_.get(ColumnNumber{} + kQueryLength);
    auto it = dictionary.find(id);
    if (it == dictionary.end()) {
      Identifier replace_id =
          std::isupper(id) ? std::tolower(id) : std::toupper(id);
      it = dictionary.find(replace_id);
      LOG(INFO) << "Looking for suplemental match: "
                << std::wstring(1, replace_id) << ": "
                << (it == dictionary.end() ? "fail" : "success");
    }
    return GoTo(it == dictionary.end() ? std::nullopt
                                       : std::make_optional(it->second));
  }

  if (input.mode == transformation::Input::Mode::kFinal)
    return futures::Past(Output());
  VisualOverlayMap overlays;
  for (std::pair<Identifier, std::map<Identifier, LineColumn>> group :
       matches) {
    for (std::pair<Identifier, LineColumn> match : group.second) {
      static const VisualOverlayPriority kPriority = VisualOverlayPriority(1);
      static const VisualOverlayKey kKey = VisualOverlayKey(L"bisect");
      Line line = input.buffer.contents().snapshot().at(match.second.line);
      overlays[kPriority][kKey].insert(std::make_pair(
          match.second,
          infrastructure::screen::VisualOverlay{
              .content = line.Substring(match.second.column, kQueryLength),
              .modifiers = {LineModifier::kUnderline},
              .behavior =
                  infrastructure::screen::VisualOverlay::Behavior::kToggle}));
      overlays[kPriority][kKey].insert(std::make_pair(
          match.second + kQueryLength,
          infrastructure::screen::VisualOverlay{
              .content =
                  SingleLine{LazyString{ColumnNumberDelta{1}, match.first}},
              .modifiers =
                  LineModifierSet{LineModifier::kReverse, LineModifier::kWhite},
              .behavior =
                  infrastructure::screen::VisualOverlay::Behavior::kToggle}));
    }
  }
  return futures::Past(
      Output(VisualOverlay{.visual_overlay_map = std::move(overlays)}));
}
}  // namespace afc::editor::transformation
