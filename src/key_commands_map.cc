#include "src/key_commands_map.h"

#include <ranges>

#include "src/help_command.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/tests/tests.h"

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Padding;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor::operation {
/* static */ NonNull<std::shared_ptr<LazyString>> KeyCommandsMap::ToString(
    Category category) {
  switch (category) {
    case KeyCommandsMap::Category::kStringControl:
      return NewLazyString(L"String");
    case KeyCommandsMap::Category::kRepetitions:
      return NewLazyString(L"Repetitions");
    case KeyCommandsMap::Category::kDirection:
      return NewLazyString(L"Direction");
    case KeyCommandsMap::Category::kStructure:
      return NewLazyString(L"Structure");
    case KeyCommandsMap::Category::kNewCommand:
      return NewLazyString(L"Command");
    case KeyCommandsMap::Category::kTop:
      return NewLazyString(L"Top");
  }
  LOG(FATAL) << "Invalid category.";
  return NewLazyString(L"");
}

void KeyCommandsMap::ExtractDescriptions(
    std::set<wchar_t>& consumed,
    std::map<Category, std::map<wchar_t, Description>>& output) const {
  for (const std::pair<const wchar_t, KeyCommand>& entry : table_)
    if (entry.second.active && consumed.insert(entry.first).second)
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
  std::set<wchar_t> consumed;
  for (const KeyCommandsMap& entry : sequence_)
    entry.ExtractDescriptions(consumed, descriptions);

  ColumnNumberDelta longest_category;
  for (const KeyCommandsMap::Category& category :
       descriptions | std::views::keys)
    longest_category =
        std::max(longest_category, KeyCommandsMap::ToString(category)->size());

  for (const std::pair<const KeyCommandsMap::Category,
                       std::map<wchar_t, Description>>& category_entry :
       descriptions) {
    LineBuilder category_line;
    NonNull<std::shared_ptr<LazyString>> category_name =
        KeyCommandsMap::ToString(category_entry.first);
    category_line.AppendString(
        Padding(longest_category - category_name->size(), L' '));
    category_line.AppendString(category_name,
                               LineModifierSet{LineModifier::kBold});
    category_line.AppendString(NewLazyString(L":"));
    // We use an inverted map to group commands with identical descriptions.
    std::map<Description, std::set<wchar_t>> inverted_map;
    for (const std::pair<const wchar_t, Description>& entry :
         category_entry.second)
      if (entry.second != Description(L""))
        inverted_map[entry.second].insert(entry.first);
    for (const std::pair<const Description, std::set<wchar_t>>& entry :
         inverted_map) {
      category_line.AppendString(NewLazyString(L" "));
      category_line.AppendString(NewLazyString(entry.first.read()),
                                 LineModifierSet{LineModifier::kCyan});
      category_line.AppendString(NewLazyString(L":"),
                                 LineModifierSet{LineModifier::kDim});
      for (wchar_t c : entry.second)
        category_line.Append(LineBuilder(DescribeSequence(std::wstring(1, c))));
    }
    help_output.push_back(
        MakeNonNullShared<Line>(std::move(category_line).Build()));
  }
  if (help_output.size() > LineNumberDelta(1) &&
      help_output.snapshot().front()->empty())
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
            map.Insert(L'a',
                       {.category = KeyCommandsMap::Category::kStringControl,
                        .description = Description(L"Test"),
                        .handler = [&executed](wchar_t) {
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
            map.Insert(L'b',
                       {.category = KeyCommandsMap::Category::kStringControl,
                        .description = Description(L"Test"),
                        .handler = [&executed](wchar_t) { executed = true; }});
            map.Erase(L'b');
            CHECK(!map.Execute(L'b'));
            CHECK(!executed);
          }},
     {.name = L"FallbackFunctionality",
      .callback =
          [] {
            KeyCommandsMap map;
            bool fallback_executed = false;
            map.SetFallback({}, [&fallback_executed](wchar_t) {
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
            std::set<wchar_t> exclude = {L'y'};
            map.SetFallback(exclude, [&fallback_executed](wchar_t) {
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
            map.Insert(L'c',
                       {.category = KeyCommandsMap::Category::kDirection,
                        .description = Description(L"Test callback"),
                        .handler = [](wchar_t) { /* Handler code here */ }});
            CHECK(map.FindCallbackOrNull(L'c') != nullptr);
          }},
     {.name = L"OnHandleExecution",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            map.OnHandle([&on_handle_executions] { on_handle_executions++; })
                .Insert(L'd',
                        {.category = KeyCommandsMap::Category::kStructure,
                         .description = Description(L"OnHandle test"),
                         .handler = [](wchar_t) { /* Handler code here */ }});
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
            std::set<wchar_t> exclude = {L'f'};
            map.SetFallback(exclude, [](wchar_t) { /* Fallback handler */ })
                .OnHandle([&on_handle_executions] { on_handle_executions++; });
            map.Execute(L'f');  // Try executing an excluded command.
            CHECK_EQ(on_handle_executions, 0ul);
          }},
     {.name = L"OnHandleRunsForFallback",
      .callback =
          [] {
            size_t on_handle_executions = 0;
            KeyCommandsMap map;
            map.SetFallback({}, [](wchar_t) { /* Fallback handler */ })
                .OnHandle([&on_handle_executions] { on_handle_executions++; });
            map.Execute(L'g');  // Trigger fallback execution.
            CHECK_EQ(on_handle_executions, 1ul);
          }},
     {.name = L"ExecuteSpecificHandlerOnly",
      .callback =
          [] {
            KeyCommandsMap map;
            size_t execution_count[3] = {0, 0, 0};

            map.Insert(L'0',
                       {.category = KeyCommandsMap::Category::kStringControl,
                        .description = Description(L"Execute0"),
                        .handler = [&](wchar_t) { execution_count[0]++; }})
                .Insert(L'1',
                        {.category = KeyCommandsMap::Category::kRepetitions,
                         .description = Description(L"Execute1"),
                         .handler = [&](wchar_t) { execution_count[1]++; }})
                .Insert(L'2',
                        {.category = KeyCommandsMap::Category::kDirection,
                         .description = Description(L"Execute2"),
                         .handler = [&](wchar_t) { execution_count[2]++; }});

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

        map.Insert(L'0', {.category = KeyCommandsMap::Category::kStringControl,
                          .description = Description(L"Handler for '0'"),
                          .handler =
                              [&executions](wchar_t c) {
                                CHECK(c == L'0');
                                executions++;
                              }})
            .Insert(L'1', {.category = KeyCommandsMap::Category::kRepetitions,
                           .description = Description(L"Handler for '1'"),
                           .handler =
                               [&executions](wchar_t c) {
                                 CHECK(c == L'1');
                                 executions++;
                               }})
            .Insert(L'2', {.category = KeyCommandsMap::Category::kDirection,
                           .description = Description(L"Handler for '2'"),
                           .handler = [&executions](wchar_t c) {
                             CHECK(c == L'2');
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