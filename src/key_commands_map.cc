#include "src/key_commands_map.h"

#include <ranges>

#include "src/help_command.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/tests/tests.h"

using afc::infrastructure::ExtendedChar;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor::operation {
/* static */ LazyString KeyCommandsMap::ToString(Category category) {
  switch (category) {
    case KeyCommandsMap::Category::kStringControl:
      return LazyString{L"String"};
    case KeyCommandsMap::Category::kRepetitions:
      return LazyString{L"Repetitions"};
    case KeyCommandsMap::Category::kDirection:
      return LazyString{L"Direction"};
    case KeyCommandsMap::Category::kStructure:
      return LazyString{L"Structure"};
    case KeyCommandsMap::Category::kNewCommand:
      return LazyString{L"Command"};
    case KeyCommandsMap::Category::kTop:
      return LazyString{L"Top"};
  }
  LOG(FATAL) << "Invalid category.";
  return LazyString{};
}

void KeyCommandsMap::ExtractDescriptions(
    std::set<ExtendedChar>& consumed,
    std::map<Category, std::map<ExtendedChar, Description>>& output) const {
  for (const std::pair<const ExtendedChar, KeyCommand>& entry : table_)
    if (entry.second.active && consumed.insert(entry.first).second)
      output[entry.second.category].insert(
          {entry.first, entry.second.description});
}

std::map<ExtendedChar, KeyCommandsMap::Category>
KeyCommandsMapSequence::GetKeys() const {
  std::map<ExtendedChar, KeyCommandsMap::Category> output;
  for (const KeyCommandsMap& entry : sequence_) {
    entry.ExtractKeys(output);
    if (entry.HasFallback()) break;
  }
  return output;
}

Line KeyCommandsMapSequence::SummaryLine() const {
  LineBuilder output;
  std::map<KeyCommandsMap::Category, std::wstring> entries_by_category;
  for (const std::pair<const ExtendedChar, KeyCommandsMap::Category>& entry :
       GetKeys())
    if (const wchar_t* regular_c = std::get_if<wchar_t>(&entry.first);
        regular_c != nullptr && isprint(*regular_c))
      entries_by_category[entry.second].push_back(*regular_c);
  for (const std::pair<const KeyCommandsMap::Category, std::wstring>& category :
       entries_by_category) {
    // TODO(trivial, 2024-09-19): Change category.second to NonEmptySingleLine.
    // Avoid wrapping it here.
    output.AppendString(
        SingleLine::Char<L' '>() + SingleLine{LazyString{category.second}},
        LineModifierSet{LineModifier::kDim});
  }
  return std::move(output).Build();
}

LineSequence KeyCommandsMapSequence::Help() const {
  MutableLineSequence help_output;
  std::map<KeyCommandsMap::Category, std::map<ExtendedChar, Description>>
      descriptions;
  std::set<ExtendedChar> consumed;
  for (const KeyCommandsMap& entry : sequence_)
    entry.ExtractDescriptions(consumed, descriptions);

  ColumnNumberDelta longest_category;
  for (const KeyCommandsMap::Category& category :
       descriptions | std::views::keys)
    longest_category =
        std::max(longest_category, KeyCommandsMap::ToString(category).size());

  for (const std::pair<const KeyCommandsMap::Category,
                       std::map<ExtendedChar, Description>>& category_entry :
       descriptions) {
    LineBuilder category_line;
    LazyString category_name = KeyCommandsMap::ToString(category_entry.first);
    category_line.AppendString(
        SingleLine{LazyString{longest_category - category_name.size(), L' '}});
    // TODO(easy, 2024-09-19): Avoid having to wrap category_name.
    category_line.AppendString(SingleLine{category_name},
                               LineModifierSet{LineModifier::kBold});
    category_line.AppendString(SingleLine{LazyString{L":"}});
    // We use an inverted map to group commands with identical descriptions.
    std::map<Description, std::set<ExtendedChar>> inverted_map;
    for (const std::pair<const ExtendedChar, Description>& entry :
         category_entry.second)
      inverted_map[entry.second].insert(entry.first);
    for (const std::pair<const Description, std::set<ExtendedChar>>& entry :
         inverted_map) {
      category_line.AppendString(SingleLine::Char<L' '>());
      category_line.AppendString(entry.first.read().read(),
                                 LineModifierSet{LineModifier::kCyan});
      category_line.AppendString(SingleLine::Char<L':'>(),
                                 LineModifierSet{LineModifier::kDim});
      for (ExtendedChar c : entry.second)
        category_line.Append(LineBuilder(DescribeSequence({c})));
    }
    help_output.push_back(std::move(category_line).Build());
  }
  if (help_output.size() > LineNumberDelta(1) &&
      help_output.snapshot().front().empty())
    help_output.EraseLines(LineNumber(), LineNumber(1));
  return help_output.snapshot();
}

namespace {
const bool key_commands_map_tests_registration = tests::Register(
    L"KeyCommandsMap",
    {{.name = L"ExecuteReturnsFalseIfNotRegistered",
      .callback = [] { CHECK(!KeyCommandsMap().Execute(L'x')); }},
     {.name = L"Insert",
      .callback =
          [] {
            KeyCommandsMap map;
            bool executed = false;
            map.Insert(
                L'a', {.category = KeyCommandsMap::Category::kStringControl,
                       .description =
                           Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Test")},
                       .handler = [&executed](ExtendedChar) {
                         CHECK(!executed);
                         executed = true;
                       }});
            CHECK(map.Execute(L'a'));
            CHECK(executed);
          }},
     {.name = L"Erase",
      .callback =
          [] {
            KeyCommandsMap map;
            bool executed = false;
            map.Insert(
                L'b',
                {.category = KeyCommandsMap::Category::kStringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Test")},
                 .handler = [&executed](ExtendedChar) { executed = true; }});
            map.Erase(L'b');
            CHECK(!map.Execute(L'b'));
            CHECK(!executed);
          }},
     {.name = L"FallbackFunctionality",
      .callback =
          [] {
            KeyCommandsMap map;
            bool fallback_executed = false;
            map.SetFallback({}, [&fallback_executed](ExtendedChar) {
              CHECK(!fallback_executed);
              fallback_executed = true;
            });
            CHECK(map.Execute(L'x'));
            CHECK(fallback_executed);
          }},
     {.name = L"FallbackExclusion",
      .callback =
          [] {
            KeyCommandsMap map;
            bool fallback_executed = false;
            std::set<ExtendedChar> exclude = {L'y'};
            map.SetFallback(exclude, [&fallback_executed](ExtendedChar) {
              fallback_executed = true;
            });
            CHECK(!map.Execute(L'y'));
            CHECK(!fallback_executed);
          }},
     {.name = L"FindCallbackNullForUnregistered",
      .callback =
          [] { CHECK(KeyCommandsMap().FindCallbackOrNull(L'z') == nullptr); }},
     {.name = L"FindCallbackNotNull",
      .callback =
          [] {
            KeyCommandsMap map;
            map.Insert(
                L'c',
                {.category = KeyCommandsMap::Category::kDirection,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Test")},
                 .handler = [](ExtendedChar) { /* Handler code here */ }});
            CHECK(map.FindCallbackOrNull(L'c') != nullptr);
          }},
     {.name = L"OnHandleExecution",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            map.OnHandle([&on_handle_executions] { on_handle_executions++; })
                .Insert(
                    L'd',
                    {.category = KeyCommandsMap::Category::kStructure,
                     .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                         L"OnHandle test")},
                     .handler = [](ExtendedChar) { /* Handler code here */ }});
            for (size_t i = 0; i < 5; i++) {
              CHECK_EQ(on_handle_executions, i);
              map.Execute(L'd');
              CHECK_EQ(on_handle_executions, i + 1);
            }
          }},
     {.name = L"OnHandleNotRunForNotFoundCommand",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            map.OnHandle([&on_handle_executions] { on_handle_executions++; });
            map.Execute(L'e');  // Try executing an unregistered command.
            CHECK_EQ(on_handle_executions, 0ul);
          }},
     {.name = L"OnHandleNotRunForExcludedFallback",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            std::set<ExtendedChar> exclude = {L'f'};
            map.SetFallback(exclude,
                            [](ExtendedChar) { /* Fallback handler */ })
                .OnHandle([&on_handle_executions] { on_handle_executions++; });
            map.Execute(L'f');  // Try executing an excluded command.
            CHECK_EQ(on_handle_executions, 0ul);
          }},
     {.name = L"OnHandleRunsForFallback",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            map.SetFallback({}, [](ExtendedChar) { /* Fallback handler */ })
                .OnHandle([&on_handle_executions] { on_handle_executions++; });
            map.Execute(L'g');  // Trigger fallback execution.
            CHECK_EQ(on_handle_executions, 1ul);
          }},
     {.name = L"ExecuteSpecificHandlerOnly",
      .callback =
          [] {
            KeyCommandsMap map;
            size_t execution_count[3] = {0, 0, 0};

            map.Insert(
                   L'0',
                   {.category = KeyCommandsMap::Category::kStringControl,
                    .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                        L"Execute0")},
                    .handler = [&](ExtendedChar) { execution_count[0]++; }})
                .Insert(
                    L'1',
                    {.category = KeyCommandsMap::Category::kRepetitions,
                     .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                         L"Execute1")},
                     .handler = [&](ExtendedChar) { execution_count[1]++; }})
                .Insert(
                    L'2',
                    {.category = KeyCommandsMap::Category::kDirection,
                     .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                         L"Execute2")},
                     .handler = [&](ExtendedChar) { execution_count[2]++; }});

            map.Execute(L'0');
            CHECK_EQ(execution_count[0], 1ul);
            CHECK_EQ(execution_count[1], 0ul);
            CHECK_EQ(execution_count[2], 0ul);

            map.Execute(L'1');
            CHECK_EQ(execution_count[0], 1ul);
            CHECK_EQ(execution_count[1], 1ul);
            CHECK_EQ(execution_count[2], 0ul);

            map.Execute(L'2');
            CHECK_EQ(execution_count[0], 1ul);
            CHECK_EQ(execution_count[1], 1ul);
            CHECK_EQ(execution_count[2], 1ul);
          }},
     {.name = L"HandlerParameterCheck", .callback = [] {
        KeyCommandsMap map;
        size_t executions = 0;

        map.Insert(L'0',
                   {.category = KeyCommandsMap::Category::kStringControl,
                    .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                        L"Handler for '0'")},
                    .handler =
                        [&executions](ExtendedChar c) {
                          CHECK(c == ExtendedChar(L'0'));
                          executions++;
                        }})
            .Insert(L'1',
                    {.category = KeyCommandsMap::Category::kRepetitions,
                     .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                         L"Handler for '1'")},
                     .handler =
                         [&executions](ExtendedChar c) {
                           CHECK(c == ExtendedChar(L'1'));
                           executions++;
                         }})
            .Insert(L'2',
                    {.category = KeyCommandsMap::Category::kDirection,
                     .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                         L"Handler for '2'")},
                     .handler = [&executions](ExtendedChar c) {
                       CHECK(c == ExtendedChar(L'2'));
                       executions++;
                     }});

        map.Execute(L'0');
        CHECK_EQ(executions, 1ul);

        map.Execute(L'1');
        CHECK_EQ(executions, 2ul);

        map.Execute(L'2');
        CHECK_EQ(executions, 3ul);
      }}});
}  // namespace
}  // namespace afc::editor::operation
