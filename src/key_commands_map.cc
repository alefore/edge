#include "src/key_commands_map.h"

#include "src/help_command.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor::operation {
/* static */ std::wstring KeyCommandsMap::ToString(Category category) {
  switch (category) {
    case KeyCommandsMap::Category::kStringControl:
      return L"String";
    case KeyCommandsMap::Category::kRepetitions:
      return L"Repetitions";
    case KeyCommandsMap::Category::kDirection:
      return L"Direction";
    case KeyCommandsMap::Category::kStructure:
      return L"Structure";
    case KeyCommandsMap::Category::kNewCommand:
      return L"Command";
    case KeyCommandsMap::Category::kTop:
      return L"Top";
  }
  LOG(FATAL) << "Invalid category.";
  return L"";
}

void KeyCommandsMap::ExtractDescriptions(
    std::map<Category, std::map<wchar_t, Description>>& output) const {
  for (const std::pair<const wchar_t, KeyCommand>& entry : table_)
    if (entry.second.active)
      output[entry.second.category].insert(
          {entry.first, entry.second.description});
}

std::map<wchar_t, KeyCommandsMap::Category> KeyCommandsMapSequence::GetKeys()
    const {
  std::map<wchar_t, KeyCommandsMap::Category> output;
  for (const KeyCommandsMap& entry : sequence_) {
    entry.ExtractKeys(output);
    if (entry.HasFallback()) break;
  }
  return output;
}

Line KeyCommandsMapSequence::SummaryLine() const {
  LineBuilder output;
  std::map<KeyCommandsMap::Category, std::wstring> entries_by_category;
  for (const std::pair<const wchar_t, KeyCommandsMap::Category>& entry :
       GetKeys())
    if (isprint(entry.first))
      entries_by_category[entry.second].push_back(entry.first);
  for (const std::pair<const KeyCommandsMap::Category, std::wstring>& category :
       entries_by_category) {
    output.AppendString(L" ", std::nullopt);
    output.AppendString(category.second, LineModifierSet{LineModifier::kDim});
  }
  return std::move(output).Build();
}

LineSequence KeyCommandsMapSequence::Help() const {
  MutableLineSequence help_output;
  std::map<KeyCommandsMap::Category, std::map<wchar_t, Description>>
      descriptions;
  for (const KeyCommandsMap& entry : sequence_)
    entry.ExtractDescriptions(descriptions);
  for (const std::pair<const KeyCommandsMap::Category,
                       std::map<wchar_t, Description>>& category_entry :
       descriptions) {
    LineBuilder category_line;
    category_line.AppendString(
        NewLazyString(KeyCommandsMap::ToString(category_entry.first)),
        LineModifierSet{LineModifier::kBold});
    category_line.AppendString(NewLazyString(L":"));
    for (const std::pair<const wchar_t, Description>& entry :
         category_entry.second)
      if (entry.second != Description(L"")) {
        category_line.AppendString(NewLazyString(L" "));
        category_line.AppendString(NewLazyString(entry.second.read()),
                                   LineModifierSet{LineModifier::kCyan});
        category_line.AppendString(NewLazyString(L":"),
                                   LineModifierSet{LineModifier::kDim});
        category_line.AppendString(
            NewLazyString(DescribeSequence(std::wstring(1, entry.first))));
      }
    help_output.push_back(
        MakeNonNullShared<Line>(std::move(category_line).Build()));
  }
  if (help_output.size() > LineNumberDelta(1))
    help_output.EraseLines(LineNumber(), LineNumber());
  return help_output.snapshot();
}

}  // namespace afc::editor::operation