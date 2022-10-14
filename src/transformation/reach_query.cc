#include "src/transformation/reach_query.h"

#include "src/buffer.h"
#include "src/buffer_display_data.h"
#include "src/buffer_variables.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/visual_overlay.h"

namespace afc::editor::transformation {
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::NewLazyString;

namespace {
static const ColumnNumberDelta kQueryLength = ColumnNumberDelta(2);

const Line& GetLine(const LineColumn& position, const OpenBuffer& buffer) {
  return buffer.contents().at(position.line).value();
}

std::vector<LineColumn> FindPositions(const std::wstring& query,
                                      const OpenBuffer& buffer) {
  std::vector<LineColumn> output;
  LineColumn view_start = buffer.Read(buffer_variables::view_start);
  std::optional<LineColumnDelta> view_size =
      buffer.display_data().view_size().Get();
  if (view_size == std::nullopt) return output;
  LineNumber end_line = view_start.line + view_size->line;
  while (view_start.line < end_line && view_start.line <= buffer.EndLine()) {
    const Line& line = GetLine(view_start, buffer);
    while (view_start.column + std::max(kQueryLength + ColumnNumberDelta(1),
                                        ColumnNumberDelta(query.size())) <=
           line.EndColumn()) {
      bool match = true;
      for (size_t i = 0; i < query.size() && match; i++) {
        match = std::tolower(static_cast<wchar_t>(
                    line.get(view_start.column + ColumnNumberDelta(i)))) ==
                std::tolower(query[i]);
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

bool FindSyntheticIdentifier(LineColumn position, const OpenBuffer& buffer,
                             PositionIdentifierMap& output) {
  static const std::wstring kIdentifiers =
      L"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const Line& line = GetLine(position, buffer);
  const Identifier first_identifier =
      line.get(position.column + ColumnNumberDelta(1));
  const Identifier desired_identifier =
      line.get(position.column + ColumnNumberDelta(2));
  size_t start_position = kIdentifiers.find_first_of(desired_identifier);
  for (size_t i = 0; i < kIdentifiers.size(); i++) {
    Identifier candidate =
        kIdentifiers[(start_position + i) % kIdentifiers.size()];
    if (output[first_identifier].insert({candidate, position}).second)
      return true;
  }
  return false;
}

PositionIdentifierMap FindIdentifiers(std::vector<LineColumn> matches,
                                      const OpenBuffer& buffer) {
  PositionIdentifierMap output;
  std::vector<LineColumn> pending;

  // Copy all elements in matches to `output` or `pending`. `pending` will
  // contain the ones where their desired identifier was already used by a
  // different position. This lets us bias towards reducing the number of
  // "invented" identifiers.
  for (LineColumn position : matches) {
    const Line& line = GetLine(position, buffer);
    const Identifier desired_identifier =
        line.get(position.column + kQueryLength);
    if (!output[line.get(position.column + ColumnNumberDelta(1))]
             .insert({desired_identifier, position})
             .second)
      pending.push_back(position);
  }

  // There's a ~square-complexity algorithm here, when `output` gets full. We
  // could remember external identifiers for which this has happened and give up
  // on them.
  for (LineColumn position : pending) {
    FindSyntheticIdentifier(position, buffer, output);
  }
  return output;
}
}  // namespace

ReachQueryTransformation::ReachQueryTransformation(std::wstring query)
    : query_(std::move(query)) {}

std::wstring ReachQueryTransformation::Serialize() const {
  return L"ReachQueryTransformation()";
}

futures::Value<CompositeTransformation::Output> ReachQueryTransformation::Apply(
    CompositeTransformation::Input input) const {
  if (query_.empty()) return futures::Past(Output());

  if (ColumnNumberDelta(query_.size()) <= kQueryLength + ColumnNumberDelta(1)) {
    if (PositionIdentifierMap matches = FindIdentifiers(
            FindPositions(query_.substr(0, kQueryLength.read()), input.buffer),
            input.buffer);
        !matches.empty()) {
      if (ColumnNumberDelta(query_.size()) ==
          kQueryLength + ColumnNumberDelta(1)) {
        Output output(VisualOverlay(VisualOverlayMap{}));
        if (auto it = matches[query_[1]].find(query_[kQueryLength.read()]);
            it != matches[query_[1]].end()) {
          LOG(INFO) << "Found destination:  " << it->second;
          output.Push(SetPosition(it->second));
        }
        return futures::Past(std::move(output));
      }
      VisualOverlayMap overlays;
      for (std::pair<Identifier, std::map<Identifier, LineColumn>> group :
           matches) {
        for (std::pair<Identifier, LineColumn> match : group.second) {
          const Line& line = GetLine(match.second, input.buffer);
          overlays.insert(std::make_pair(
              match.second,
              afc::editor::VisualOverlay{
                  .content = line.Substring(match.second.column, kQueryLength)
                                 .get_shared(),
                  .modifiers = {LineModifier::UNDERLINE}}));
          overlays.insert(std::make_pair(
              match.second + kQueryLength,
              afc::editor::VisualOverlay{
                  .content =
                      std::move(NewLazyString(ColumnNumberDelta(1), match.first)
                                    .get_unique()),
                  .modifiers = LineModifierSet{LineModifier::REVERSE,
                                               LineModifier::WHITE}}));
        }
      }
      return futures::Past(Output(VisualOverlay(std::move(overlays))));
    }
  }

  return futures::Past(Output());
}
}  // namespace afc::editor::transformation
