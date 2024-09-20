#include "buffers_list.h"

#include <algorithm>
#include <ctgmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/overload.h"
#include "src/tests/tests.h"
#include "src/widget.h"
#include "src/widget_list.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::GetElapsedSecondsSince;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EraseIf;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;

namespace afc::editor {
namespace {
ValueOrError<LineBuilder> GetOutputComponents(
    const std::list<PathComponent>& components, ColumnNumberDelta columns,
    const LineModifierSet& modifiers, const LineModifierSet& bold,
    const LineModifierSet& dim) {
  if (components.empty()) return Error{LazyString{L"Empty components"}};

  std::list<LineBuilder> output_items;

  // We try to reserve at least one character for each path (except the last)
  // and for each separator. But we give the last character priority.
  ColumnNumberDelta reserved = std::min(
      ColumnNumberDelta(components.size() - 1) +
          ColumnNumberDelta(1) * (components.size() - 1),
      columns - std::min(columns, ColumnNumberDelta(components.back().size())));
  columns -= reserved;

  for (const PathComponent& path_full : components | std::views::reverse) {
    if (columns.IsZero() && reserved.IsZero()) break;
    LineBuilder current_output;
    auto Add = [&current_output, &columns](SingleLine s,
                                           const LineModifierSet& m) {
      CHECK_LE(s.size(), columns);
      current_output.AppendString(s, m);
      columns -= s.size();
    };

    ColumnNumberDelta separator_size =
        output_items.empty() ? ColumnNumberDelta(0) : ColumnNumberDelta(1);

    if (!output_items.empty()) {
      ColumnNumberDelta desired = ColumnNumberDelta(1) + separator_size;
      // The reason we add `ColumnNumberDelta(1)` is that leaving just 1
      // character in `reserved` is pointless. If there's only 1 character, we
      // might as well use it now.
      if (desired + ColumnNumberDelta(1) >= reserved) {
        columns += reserved;
        reserved = ColumnNumberDelta();
      } else {
        reserved -= desired;
        columns += desired;
      }
      CHECK_GE(reserved, ColumnNumberDelta(0));
    }

    if (columns == ColumnNumberDelta(1)) {
      Add(SingleLine::Char<L'…'>(), dim);
    } else {
      ASSIGN_OR_RETURN(
          PathComponent path,
          ColumnNumberDelta(path_full.size()) + separator_size <= columns
              ? path_full
              : PathComponent::New(
                    output_items.empty()
                        ? path_full.read().Substring(ColumnNumber{} +
                                                     path_full.size() -
                                                     columns + separator_size)
                        : path_full.read().Substring(
                              ColumnNumber{}, columns - separator_size)));
      if (output_items.empty())
        std::visit(
            overload{
                [&](Error) {
                  Add(LineSequence::BreakLines(path.read()).FoldLines(),
                      modifiers);
                },
                [&](const PathComponent& path_without_extension) {
                  if (std::optional<LazyString> extension = path.extension();
                      extension.has_value()) {
                    Add(LineSequence::BreakLines(path_without_extension.read())
                            .FoldLines(),
                        bold);
                    Add(SingleLine::Char<L'.'>(), dim);
                    Add(LineSequence::BreakLines(extension.value()).FoldLines(),
                        bold);
                  } else {
                    Add(LineSequence::BreakLines(path.read()).FoldLines(),
                        modifiers);
                  }
                }},
            path.remove_extension());
      else if (columns > ColumnNumberDelta(1)) {
        Add(LineSequence::BreakLines(path.read()).FoldLines(), modifiers);
        Add(path == path_full ? SingleLine::Char<L'/'>()
                              : SingleLine::Char<L'…'>(),
            dim);
      }
    }
    output_items.push_front(std::move(current_output));
  }

  LineBuilder output = container::Fold(
      [](LineBuilder a, LineBuilder b) -> LineBuilder {
        b.Append(std::move(a));
        return b;
      },
      LineBuilder(), std::move(output_items));
  return output;
}

SingleLine GetOutputComponentsForTesting(std::wstring path,
                                         ColumnNumberDelta columns) {
  SingleLine output =
      ValueOrDie(
          GetOutputComponents(
              ValueOrDie(
                  ValueOrDie(Path::New(LazyString{path})).DirectorySplit()),
              columns, LineModifierSet{}, LineModifierSet{}, LineModifierSet{}))
          .Build()
          .contents();
  LOG(INFO) << "GetOutputComponentsForTesting: " << path << " -> " << output;
  return output;
}

const bool get_output_components_tests_registration = tests::Register(
    L"GetOutputComponents",
    {{.name = L"SingleFits",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"foo";
            CHECK_EQ(
                GetOutputComponentsForTesting(kMessage, ColumnNumberDelta(80)),
                SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"SingleTrim",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"lejandro";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro",
                                                   ColumnNumberDelta(8)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"SingleFitsExactly",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"alejandro";
            CHECK_EQ(
                GetOutputComponentsForTesting(kMessage, ColumnNumberDelta(9)),
                SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleFits",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"alejandro/forero/cuervo";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(80)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleFitsExactly",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"alejandro/forero/cuervo";

            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(23)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimFirst",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"alejandr…forero/cuervo";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(22)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimSignificant",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"a…f…cuervo";
            CHECK_EQ(GetOutputComponentsForTesting(
                         L"alejandro/forero/cuervo",
                         ColumnNumberDelta((1 + 1) + (1 + 1) + 6)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimSpill",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"fo…cuervo";
            CHECK_EQ(
                GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                              ColumnNumberDelta(2 + 1 + 6)),
                SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimToFirst",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"uervo";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(5)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimExact",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"cuervo";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(6)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimUnusedSpill",
      .callback =
          [] {
            static constexpr wchar_t kMessage[] = L"…cuervo";
            CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                   ColumnNumberDelta(7)),
                     SingleLine::FromConstant<kMessage>());
          }},
     {.name = L"MultipleTrimSmallSpill", .callback = [] {
        static constexpr wchar_t kMessage[] = L"f…cuervo";
        CHECK_EQ(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                               ColumnNumberDelta(8)),
                 SingleLine::FromConstant<kMessage>());
      }}});

// Converts the `std::vector` of `BuffersList::filter_` to an
// `std::unordered_set` for faster access. For convenience, also takes care of
// resolving the `weak_ptr` buffers to their actual addresses (skipping
// expired buffers).
std::optional<std::unordered_set<NonNull<const OpenBuffer*>>> OptimizeFilter(
    const std::optional<std::vector<gc::WeakPtr<OpenBuffer>>> input) {
  if (!input.has_value()) {
    return std::nullopt;
  }

  std::unordered_set<NonNull<const OpenBuffer*>> output;
  for (auto& weak_buffer : input.value()) {
    VisitPointer(
        weak_buffer.Lock(),
        [&](gc::Root<OpenBuffer> buffer) {
          output.insert(
              NonNull<const OpenBuffer*>::AddressOf(buffer.ptr().value()));
        },
        [] {});
  }
  return output;
}

struct BuffersListOptions {
  const std::vector<gc::Root<OpenBuffer>>& buffers;
  std::optional<gc::Root<OpenBuffer>> active_buffer;
  std::set<NonNull<const OpenBuffer*>> active_buffers;
  size_t buffers_per_line;
  LineColumnDelta size;
  std::optional<std::unordered_set<NonNull<const OpenBuffer*>>> filter;
};

enum class FilterResult { kExcluded, kIncluded };

LineModifierSet GetNumberModifiers(const BuffersListOptions& options,
                                   const OpenBuffer& buffer,
                                   FilterResult filter_result) {
  LineModifierSet output;
  if (buffer.status().GetType() == Status::Type::kWarning) {
    output.insert(LineModifier::kRed);
    const double kSecondsWarningHighlight = 5;
    if (GetElapsedSecondsSince(buffer.status().last_change_time()) <
        kSecondsWarningHighlight) {
      output.insert(LineModifier::kReverse);
    }
  } else if (filter_result == FilterResult::kExcluded) {
    output.insert(LineModifier::kDim);
  } else if (buffer.child_pid().has_value()) {
    output.insert(LineModifier::kYellow);
  } else if (buffer.child_exit_status().has_value()) {
    auto status = buffer.child_exit_status().value();
    if (!WIFEXITED(status)) {
      output.insert(LineModifier::kRed);
      output.insert(LineModifier::kBold);
    } else if (WEXITSTATUS(status) == 0) {
      output.insert(LineModifier::kGreen);
    } else {
      output.insert(LineModifier::kRed);
    }
    if (GetElapsedSecondsSince(buffer.time_last_exit()) < 5.0) {
      output.insert({LineModifier::kReverse});
    }
  } else {
    if (buffer.dirty()) {
      output.insert(LineModifier::kItalic);
    }
    output.insert(LineModifier::kCyan);
  }
  if (options.active_buffer.has_value() &&
      &buffer == &options.active_buffer.value().ptr().value()) {
    output.insert(LineModifier::kBold);
    output.insert(LineModifier::kReverse);
  }
  return output;
}

std::vector<std::list<PathComponent>> RemoveCommonPrefixes(
    std::vector<std::list<PathComponent>> output) {
  std::list<PathComponent>* prefix_list = nullptr;
  for (auto& entry : output)
    if (!entry.empty()) prefix_list = &entry;
  if (prefix_list == nullptr) return output;
  while (true) {
    // Verify that the first entry in prefix is a prefix of every entry.
    PathComponent* prefix = &prefix_list->front();
    for (std::list<PathComponent>& entry : output) {
      if (entry.empty()) continue;
      if (entry.front() != *prefix) return output;
      if (entry.size() == 1) {
        VLOG(5) << "RemoveCommonPrefixes giving up: Entry would become empty.";
        return output;
      }
    }

    // Remove the first entry from every item.
    for (auto& entry : output)
      if (!entry.empty()) entry.pop_front();
  }
  return output;
}

std::vector<std::wstring> RemoveCommonPrefixesForTesting(
    std::vector<std::wstring> input) {
  return container::MaterializeVector(
      RemoveCommonPrefixes(container::MaterializeVector(
          input | std::views::transform([](const std::wstring& c) {
            return std::visit(
                overload{[](Error) { return std::list<PathComponent>(); },
                         [](Path path) {
                           return ValueOrDie(path.DirectorySplit(),
                                             L"RemoveCommonPrefixesForTesting");
                         }},
                Path::New(LazyString{c}));
          }))) |
      std::views::transform([](std::list<PathComponent> components) {
        return components.empty()
                   ? L""
                   : container::Fold(
                         [](PathComponent c, std::optional<Path> p) {
                           return p.has_value() ? Path::Join(*p, c) : c;
                         },
                         std::optional<Path>(), components)
                         ->read()
                         .ToString();
      }));
}

const bool remove_common_prefixes_tests_registration =
    tests::Register(L"RemoveCommonPrefixes", []() -> std::vector<tests::Test> {
      const std::list<PathComponent> empty_list;
      using Set = std::vector<std::wstring>;
      return {
          {.name = L"EmptyVector",
           .callback = [] { CHECK(RemoveCommonPrefixes({}).empty()); }},
          {.name = L"NonEmptyVectorEmptyLists",
           .callback =
               [&] {
                 Set empty_paths = {L"", L"", L""};
                 CHECK(RemoveCommonPrefixesForTesting(empty_paths) ==
                       empty_paths);
               }},
          {.name = L"SingleVectorEmptyList",
           .callback =
               [] {
                 Set empty_paths = {L""};
                 CHECK(RemoveCommonPrefixesForTesting(empty_paths) ==
                       empty_paths);
               }},
          {.name = L"NonEmptyVectorNoCommonPrefix",
           .callback =
               [&] {
                 Set non_overlapping_paths = {L"a/b/c", L"a/b/c/d", L"z/x/y"};
                 CHECK(RemoveCommonPrefixesForTesting(non_overlapping_paths) ==
                       non_overlapping_paths);
               }},
          {.name = L"SomeOverlap",
           .callback =
               [&] {
                 CHECK(RemoveCommonPrefixesForTesting(
                           {L"a/b/c", L"", L"a/b/c/d/e", L"a/x/y"}) ==
                       Set({L"b/c", L"", L"b/c/d/e", L"x/y"}));
               }},
          {.name = L"LargeOverlap",
           .callback =
               [&] {
                 CHECK(RemoveCommonPrefixesForTesting(
                           {L"a/b/c", L"", L"a/b/c/d/e", L"", L"a/b/y"}) ==
                       Set({L"c", L"", L"c/d/e", L"", L"y"}));
               }},
          {.name = L"SomeOverlapButWouldLeaveEmpty", .callback = [&] {
             CHECK(RemoveCommonPrefixesForTesting(
                       {L"a/b/c", L"", L"a/b/c/d/e", L"a/x/y", L"a"}) ==
                   Set({L"a/b/c", L"", L"a/b/c/d/e", L"a/x/y", L"a"}));
           }}};
    }());

enum class SelectionState { kReceivingInput, kIdle, kExcludedByFilter };

LineBuilder GetBufferContents(const LineSequence& contents,
                              ColumnNumberDelta columns) {
  Line line = contents.at(LineNumber(0));
  LineBuilder output;
  if ((line.EndColumn() + ColumnNumberDelta(1)).ToDelta() < columns) {
    ColumnNumberDelta padding = (columns - line.EndColumn().ToDelta()) / 2;
    output.AppendString(SingleLine::Padding(padding));
  }

  LineBuilder line_without_suffix(line);
  line_without_suffix.DeleteSuffix(ColumnNumber() + columns);
  output.Append(std::move(line_without_suffix));
  output.ClearModifiers();
  output.InsertModifier(ColumnNumber{}, LineModifier::kDim);
  return output;
}

LineBuilder GetBufferVisibleString(const ColumnNumberDelta columns,
                                   SingleLine buffer_name,
                                   const LineSequence& contents,
                                   LineModifierSet modifiers,
                                   SelectionState selection_state,
                                   const std::list<PathComponent>& components) {
  std::optional<LineModifierSet> modifiers_override;

  LineModifierSet dim = modifiers;
  dim.insert(LineModifier::kDim);

  LineModifierSet bold = modifiers;

  switch (selection_state) {
    case SelectionState::kExcludedByFilter:
      modifiers.insert(LineModifier::kDim);
      bold.insert(LineModifier::kDim);
      break;
    case SelectionState::kReceivingInput:
      modifiers.insert(LineModifier::kReverse);
      modifiers.insert(LineModifier::kCyan);
      bold = dim = modifiers;
      bold.insert(LineModifier::kBold);
      break;
    case SelectionState::kIdle:
      bold.insert(LineModifier::kBold);
      break;
  }

  LineBuilder output;
  std::visit(
      overload{[&](Error) {
                 SingleLine output_name = buffer_name;
                 if (output_name.size() > ColumnNumberDelta(2) &&
                     output_name.get(ColumnNumber(0)) == L'$' &&
                     output_name.get(ColumnNumber(1)) == L' ') {
                   output_name = TrimLeft(
                       std::move(output_name).Substring(ColumnNumber(1)), L" ");
                 }
                 output.AppendString(
                     std::move(output_name)
                         .SubstringWithRangeChecks(ColumnNumber(0), columns),
                     modifiers);
                 CHECK_LE(output.size(), columns);
               },
               [&output](LineBuilder processed_components) {
                 output.Append(std::move(processed_components));
               }},
      GetOutputComponents(components, columns, modifiers, bold, dim));

  if (columns > output.EndColumn().ToDelta())
    output.Append(
        GetBufferContents(contents, columns - output.EndColumn().ToDelta()));

  CHECK_LE(output.size(), columns);
  return output;
}

const bool get_buffer_visible_string_tests_registration = tests::Register(
    L"GetBufferVisibleString",
    std::vector<tests::Test>{
        {.name = L"LongExtension", .callback = [] {
           GetBufferVisibleString(
               ColumnNumberDelta(48), SINGLE_LINE_CONSTANT(L"name_irrelevant"),
               LineSequence(), LineModifierSet{}, SelectionState::kIdle,
               ValueOrDie(
                   ValueOrDie(Path::New(LazyString{
                                  L"edge-clang/edge/src/args.cc/.edge_state"}))
                       .DirectorySplit()));
         }}});

ValueOrError<std::vector<ColumnNumberDelta>> DivideLine(
    ColumnNumberDelta total_length, ColumnNumberDelta separator_padding,
    size_t columns) {
  VLOG(5) << "DivideLine: " << total_length;
  CHECK_GE(columns, 1ul);
  ColumnNumberDelta total_padding = separator_padding * (columns - 1);
  if (total_length < total_padding)
    return Error{LazyString{L"DivideLine: total_length is too short."}};

  std::vector<ColumnNumberDelta> output(
      columns, (total_length - total_padding) / columns);
  CHECK_GT(output.size(), 0ul);
  ColumnNumberDelta remaining_characters =
      total_length - output[0] * columns - total_padding;
  CHECK_GE(remaining_characters, ColumnNumberDelta());
  CHECK_LT(remaining_characters, ColumnNumberDelta(1) * columns);
  for (size_t i = 0; remaining_characters > ColumnNumberDelta(); i++) {
    CHECK_LT(i, output.size());
    ++output[i];
    --remaining_characters;
  }
  CHECK_EQ(container::Fold(
               [](ColumnNumberDelta a, ColumnNumberDelta b) {
                 VLOG(7) << "Entry: " << a;
                 return a + b;
               },
               ColumnNumberDelta(0), output) +
               separator_padding * (output.size() - 1),
           total_length);

  return output;
}

bool divide_line_tests = tests::Register(
    L"DivideLine",
    {{.name = L"SingleColumn",
      .callback =
          [] {
            CHECK(ValueOrDie(DivideLine(ColumnNumberDelta(100),
                                        ColumnNumberDelta(5), 1)) ==
                  std::vector<ColumnNumberDelta>{ColumnNumberDelta(100)});
          }},
     {.name = L"EvenDivide",
      .callback =
          [] {
            CHECK(ValueOrDie(DivideLine(ColumnNumberDelta(21),
                                        ColumnNumberDelta(3), 3)) ==
                  std::vector<ColumnNumberDelta>({ColumnNumberDelta(5),
                                                  ColumnNumberDelta(5),
                                                  ColumnNumberDelta(5)}));
          }},
     {.name = L"SpareCharacters", .callback = [] {
        CHECK(
            ValueOrDie(DivideLine(ColumnNumberDelta(7 + 1 + 3 + 7 + 1 + 3 + 7),
                                  ColumnNumberDelta(3), 3)) ==
            std::vector<ColumnNumberDelta>({ColumnNumberDelta(8),
                                            ColumnNumberDelta(8),
                                            ColumnNumberDelta(7)}));
      }}});

ValueOrError<std::list<PathComponent>> GetPathComponentsForBuffer(
    const OpenBuffer& buffer) {
  LazyString path_str = buffer.ReadLazyString(buffer_variables::path);
  if (path_str != buffer.ReadLazyString(buffer_variables::name))
    return Error{LazyString{L"name doesn't match path."}};
  ASSIGN_OR_RETURN(Path path, Path::New(path_str));
  ASSIGN_OR_RETURN(std::list<PathComponent> components, path.DirectorySplit());
  return components;
}

LineWithCursor::Generator::Vector ProduceBuffersList(
    NonNull<std::shared_ptr<BuffersListOptions>> options) {
  TRACK_OPERATION(BuffersList__ProduceBuffersList);

  // We reserve space for the largest number, and extra space for the `progress`
  // character, and an extra space at the end.
  static const ColumnNumberDelta kProgressWidth = ColumnNumberDelta(1);
  const ColumnNumberDelta prefix_width =
      ColumnNumberDelta(
          std::max(2ul, std::to_wstring(options->buffers.size()).size())) +
      kProgressWidth;

  // Contains one element for each entry in options.buffers.
  // TODO(2023-11-26, Range): Why can't I use gc::view::Value here?
  const std::vector<std::list<PathComponent>> path_components =
      RemoveCommonPrefixes(container::MaterializeVector(
          options->buffers |
          std::views::transform([](const gc::Root<OpenBuffer>& buffer) {
            return OptionalFrom(
                       GetPathComponentsForBuffer(buffer.ptr().value()))
                .value_or(std::list<PathComponent>());
          })));
  CHECK_EQ(path_components.size(), options->buffers.size());

  VLOG(1) << "BuffersList created. Buffers per line: "
          << options->buffers_per_line << ", prefix width: " << prefix_width
          << ", count: " << options->buffers.size();

  LineWithCursor::Generator::Vector output{.lines = {},
                                           .width = options->size.column};
  size_t index = 0;
  for (LineNumberDelta i; i < options->size.line; ++i) {
    VLOG(2) << "BuffersListProducer::WriteLine start, index: " << index
            << ", buffers_per_line: " << options->buffers_per_line
            << ", size: " << options->buffers.size();

    CHECK_LT(index, options->buffers.size())
        << "Buffers per line: " << options->buffers_per_line;
    output.lines.push_back(LineWithCursor::Generator{
        std::nullopt, [options, prefix_width, path_components, index]() {
          LineBuilder line_options_output;

          static const SingleLine kSeparator = SingleLine::Char<L' '>();
          const std::vector<ColumnNumberDelta> columns_width =
              OptionalFrom(DivideLine(options->size.column, kSeparator.size(),
                                      options->buffers_per_line))
                  .value_or(std::vector(options->buffers_per_line,
                                        ColumnNumberDelta()));
          CHECK_EQ(columns_width.size(), options->buffers_per_line);

          ColumnNumber start;
          for (size_t j = 0; j < options->buffers_per_line &&
                             index + j < options->buffers.size();
               j++) {
            const OpenBuffer& buffer =
                options->buffers.at(index + j).ptr().value();
            CHECK_GE(start.ToDelta(), line_options_output.contents().size());
            line_options_output.AppendString(
                SingleLine::Padding(start.ToDelta() -
                                    line_options_output.contents().size()),
                LineModifierSet());

            if (j > 0) {
              start += kSeparator.size();
              line_options_output.AppendString(kSeparator, LineModifierSet());
            }

            FilterResult filter_result =
                (!options->filter.has_value() ||
                 options->filter.value().find(
                     NonNull<const OpenBuffer*>::AddressOf(buffer)) !=
                     options->filter.value().end())
                    ? FilterResult::kIncluded
                    : FilterResult::kExcluded;

            SingleLine number_prefix =
                SingleLine{LazyString{std::to_wstring(index + j + 1)}};
            line_options_output.AppendString(
                SingleLine::Padding(prefix_width - number_prefix.size() -
                                    kProgressWidth) +
                    number_prefix,
                GetNumberModifiers(options.value(), buffer, filter_result));

            CHECK_EQ(line_options_output.contents().size(),
                     start.ToDelta() + prefix_width - kProgressWidth);

            SingleLine progress;
            LineModifierSet progress_modifier;
            if (!buffer.GetLineMarks().empty()) {
              progress = SingleLine::Char<L'!'>();
              progress_modifier.insert(LineModifier::kRed);
            } else if (!buffer.GetExpiredLineMarks().empty()) {
              progress = SingleLine::Char<L'!'>();
            } else if (buffer.ShouldDisplayProgress()) {
              progress = ProgressString(buffer.Read(buffer_variables::progress),
                                        OverflowBehavior::kModulo)
                             .read();
            } else {
              progress = ProgressStringFillUp(buffer.lines_size().read(),
                                              OverflowBehavior::kModulo)
                             .read();
              progress_modifier.insert(LineModifier::kDim);
            }
            // If we ever make ProgressString return more than a single
            // character, we'll have to adjust this.
            CHECK_EQ(progress.size(), ColumnNumberDelta{1});

            if (columns_width[j] >= prefix_width)
              line_options_output.AppendString(
                  progress, filter_result == FilterResult::kExcluded
                                ? LineModifierSet{LineModifier::kDim}
                                : progress_modifier);

            CHECK_EQ(line_options_output.contents().size(),
                     start.ToDelta() + prefix_width);

            SelectionState selection_state;
            switch (filter_result) {
              case FilterResult::kExcluded:
                selection_state = SelectionState::kExcludedByFilter;
                break;
              case FilterResult::kIncluded:
                selection_state =
                    options->active_buffers.find(
                        NonNull<const OpenBuffer*>::AddressOf(buffer)) !=
                            options->active_buffers.end()
                        ? SelectionState::kReceivingInput
                        : SelectionState::kIdle;
            }
            if (columns_width[j] > prefix_width) {
              LineBuilder visible_string = GetBufferVisibleString(
                  columns_width[j] - prefix_width,
                  LineSequence::BreakLines(buffer.Read(buffer_variables::name))
                      .FoldLines(),
                  buffer.contents().snapshot(),
                  buffer.dirty() ? LineModifierSet{LineModifier::kItalic}
                                 : LineModifierSet{},
                  selection_state, path_components[index + j]);
              CHECK_LE(visible_string.size(), columns_width[j]);
              line_options_output.Append(std::move(visible_string));
              CHECK_LE(line_options_output.contents().size(),
                       start.ToDelta() + columns_width[j]);
            }
            start += columns_width[j];
          }
          return LineWithCursor{.line = std::move(line_options_output).Build()};
        }});
    index += options->buffers_per_line;
  }
  return output;
}
}  // namespace

BuffersList::BuffersList(NonNull<std::unique_ptr<CustomerAdapter>> customer)
    : BuffersList(std::move(customer),
                  MakeNonNullUnique<BufferWidget>(BufferWidget::Options{})) {}

BuffersList::BuffersList(NonNull<std::unique_ptr<CustomerAdapter>> customer,
                         NonNull<std::unique_ptr<BufferWidget>> widget)
    : customer_(std::move(customer)),
      active_buffer_widget_(widget.get()),
      widget_(std::move(widget)) {}

void BuffersList::AddBuffer(gc::Root<OpenBuffer> buffer,
                            AddBufferType add_buffer_type) {
  switch (add_buffer_type) {
    case AddBufferType::kVisit:
      if (std::find_if(buffers_.begin(), buffers_.end(),
                       [buffer_addr = &buffer.ptr().value()](
                           gc::Root<OpenBuffer>& candidate) {
                         return &candidate.ptr().value() == buffer_addr;
                       }) == buffers_.end()) {
        buffers_.push_back(buffer);
      }
      active_buffer_widget_->SetBuffer(buffer.ptr().ToWeakPtr());
      buffer.ptr()->Visit();
      Update();
      break;

    case AddBufferType::kOnlyList:
      if (std::find_if(buffers_.begin(), buffers_.end(),
                       [buffer_addr = &buffer.ptr().value()](
                           gc::Root<OpenBuffer>& candidate) {
                         return &candidate.ptr().value() == buffer_addr;
                       }) == buffers_.end()) {
        buffers_.push_back(buffer);
      }
      Update();
      break;

    case AddBufferType::kIgnore:
      break;
  }
}

std::vector<gc::Root<OpenBuffer>> BuffersList::GetAllBuffers() const {
  return buffers_;
}

void BuffersList::RemoveBuffers(
    const std::unordered_set<NonNull<const OpenBuffer*>>& buffers_to_erase) {
  EraseIf(buffers_, [&buffers_to_erase](const gc::Root<OpenBuffer>& candidate) {
    return buffers_to_erase.contains(
        NonNull<const OpenBuffer*>::AddressOf(candidate.ptr().value()));
  });
  Update();
}

const gc::Root<OpenBuffer>& BuffersList::GetBuffer(size_t index) const {
  CHECK_LT(index, buffers_.size());
  return buffers_[index];
}

std::optional<size_t> BuffersList::GetBufferIndex(
    const OpenBuffer& buffer) const {
  auto it = std::find_if(buffers_.begin(), buffers_.end(),
                         [&buffer](const gc::Root<OpenBuffer>& candidate) {
                           return &candidate.ptr().value() == &buffer;
                         });
  return it == buffers_.end() ? std::optional<size_t>()
                              : std::distance(buffers_.begin(), it);
}

size_t BuffersList::GetCurrentIndex() {
  std::optional<gc::Root<OpenBuffer>> buffer = active_buffer();
  if (!buffer.has_value()) {
    return 0;
  }
  return GetBufferIndex(buffer->ptr().value()).value();
}

size_t BuffersList::BuffersCount() const { return buffers_.size(); }

void BuffersList::set_filter(
    std::optional<std::vector<gc::WeakPtr<OpenBuffer>>> filter) {
  filter_ = std::move(filter);
}

BufferWidget& BuffersList::GetActiveLeaf() {
  return const_cast<BufferWidget&>(
      const_cast<const BuffersList*>(this)->GetActiveLeaf());
}

const BufferWidget& BuffersList::GetActiveLeaf() const {
  return active_buffer_widget_.value();
}

namespace {
struct Layout {
  size_t buffers_per_line;
  LineNumberDelta lines;
};

Layout BuffersPerLine(LineNumberDelta maximum_lines, ColumnNumberDelta width,
                      size_t buffers_count) {
  TRACK_OPERATION(BuffersPerLine);

  if (buffers_count == 0 || maximum_lines.IsZero()) {
    return Layout{.buffers_per_line = 0, .lines = LineNumberDelta(0)};
  }
  double count = buffers_count;
  static const auto kDesiredColumnsPerBuffer = ColumnNumberDelta(20);
  size_t max_buffers_per_line =
      width /
      std::min(kDesiredColumnsPerBuffer,
               width / static_cast<size_t>(ceil(count / maximum_lines.read())));
  auto lines = LineNumberDelta(ceil(count / max_buffers_per_line));
  return Layout{
      .buffers_per_line = static_cast<size_t>(ceil(count / lines.read())),
      .lines = lines};
}

static const auto kTestSizeLines = LineNumberDelta(10);
const bool buffers_per_line_tests_registration = tests::Register(
    L"BuffersPerLineTests",
    {{.name = L"SingleBuffer",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(100), 1);
            CHECK_EQ(layout.lines, LineNumberDelta(1));
            CHECK_EQ(layout.buffers_per_line, 1ul);
          }},
     {.name = L"SingleLine",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(100), 4);
            CHECK_EQ(layout.lines, LineNumberDelta(1));
            CHECK_EQ(layout.buffers_per_line, 4ul);
          }},
     {.name = L"SingleLineFull",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(100), 5);
            CHECK_EQ(layout.lines, LineNumberDelta(1));
            CHECK_EQ(layout.buffers_per_line, 5ul);
          }},
     {.name = L"TwoLinesJustAtBoundary",
      .callback =
          [] {
            // Identical to SingleLineFull, but the buffers don't fit
            // by 1 position.
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(99), 5);
            CHECK_EQ(layout.lines, LineNumberDelta(2));
            CHECK_EQ(layout.buffers_per_line, 3ul);
          }},
     {.name = L"ThreeLines",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(100), 11);
            CHECK_EQ(layout.lines, LineNumberDelta(3));
            CHECK_EQ(layout.buffers_per_line, 4ul);
          }},
     {.name = L"ManyLinesFits",
      .callback =
          [] {
            auto layout = BuffersPerLine(LineNumberDelta(100),
                                         ColumnNumberDelta(100), 250);
            CHECK_EQ(layout.lines, LineNumberDelta(50));
            CHECK_EQ(layout.buffers_per_line, 5ul);
          }},
     {.name = L"ZeroBuffers",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(kTestSizeLines, ColumnNumberDelta(100), 0);
            CHECK_EQ(layout.lines, LineNumberDelta(0));
            CHECK_EQ(layout.buffers_per_line, 0ul);
          }},
     {.name = L"ZeroMaximumLines",
      .callback =
          [] {
            auto layout =
                BuffersPerLine(LineNumberDelta(0), ColumnNumberDelta(100), 5);
            CHECK_EQ(layout.lines, LineNumberDelta(0));
            CHECK_EQ(layout.buffers_per_line, 0ul);
          }},
     {.name = L"Overcrowded", .callback = [] {
        // This test checks that kDesiredColumnsPerBuffer can be trimmed
        // due to maximum_lines.
        //
        // We'll produce a result that allows each buffer 7 characters.
        // With a line length of 100, that yields 14 buffers per line
        // (filling up 98 characters). With 3 lines, that yields 42
        // buffers. We subtract 1 to make it not fit in fully.
        auto layout =
            BuffersPerLine(LineNumberDelta(3), ColumnNumberDelta(100), 41);
        CHECK_EQ(layout.lines, LineNumberDelta(3));
        CHECK_EQ(layout.buffers_per_line, 14ul);
      }}});
}  // namespace

LineWithCursor::Generator::Vector BuffersList::GetLines(
    Widget::OutputProducerOptions options) const {
  TRACK_OPERATION(BuffersList__GetLines);

  Layout layout = BuffersPerLine(options.size.line / 2, options.size.column,
                                 buffers_.size());
  VLOG(1) << "Buffers list lines: " << layout.lines
          << ", size: " << buffers_.size()
          << ", size column: " << options.size.column;

  LineWithCursor::Generator::Vector output;
  if (!layout.lines.IsZero()) {
    options.size.line -= layout.lines;

    VLOG(2) << "Buffers per line: " << layout.buffers_per_line
            << ", from: " << buffers_.size()
            << " buffers with lines: " << layout.lines;

    output = ProduceBuffersList(
        MakeNonNullShared<BuffersListOptions>(BuffersListOptions{
            .buffers = buffers_,
            .active_buffer = active_buffer(),
            .active_buffers = container::MaterializeSet(
                customer_->active_buffers() | gc::view::Value |
                std::views::transform([](const OpenBuffer& b) {
                  return NonNull<const OpenBuffer*>::AddressOf(b);
                })),
            .buffers_per_line = layout.buffers_per_line,
            .size = LineColumnDelta(layout.lines, options.size.column),
            .filter = OptimizeFilter(filter_)}));
    CHECK_EQ(output.size(), layout.lines);
    output.RemoveCursor();
    output.Append(std::move(output));
  }

  LineWithCursor::Generator::Vector widget_output =
      widget_->CreateOutput(options);
  CHECK_EQ(widget_output.size(), options.size.line);
  output.Append(std::move(widget_output));

  return output;
}

std::optional<gc::Root<OpenBuffer>> BuffersList::active_buffer() const {
  return active_buffer_widget_->Lock();
}

void BuffersList::SetBufferSortOrder(BufferSortOrder buffer_sort_order) {
  buffer_sort_order_ = buffer_sort_order;
}

void BuffersList::SetBuffersToRetain(std::optional<size_t> buffers_to_retain) {
  buffers_to_retain_ = buffers_to_retain;
}

void BuffersList::SetBuffersToShow(std::optional<size_t> buffers_to_show) {
  buffers_to_show_ = buffers_to_show;
}

void BuffersList::Update() {
  TRACK_OPERATION(BuffersList__Update);

  auto order_predicate =
      buffer_sort_order_ == BufferSortOrder::kLastVisit
          ? [](const OpenBuffer& a,
               const OpenBuffer& b) { return a.last_visit() > b.last_visit(); }
          : [](const OpenBuffer& a, const OpenBuffer& b) {
              return a.name() < b.name();
            };
  std::sort(buffers_.begin(), buffers_.end(),
            [order_predicate](const gc::Root<OpenBuffer>& a,
                              const gc::Root<OpenBuffer>& b) {
              return (a.ptr()->Read(buffer_variables::pin) ==
                              b.ptr()->Read(buffer_variables::pin)
                          ? order_predicate(a.ptr().value(), b.ptr().value())
                          : a.ptr()->Read(buffer_variables::pin) >
                                b.ptr()->Read(buffer_variables::pin));
            });

  if (buffers_to_retain_.has_value() &&
      buffers_.size() > buffers_to_retain_.value()) {
    std::vector<gc::Root<OpenBuffer>> retained_buffers;
    retained_buffers.reserve(buffers_.size());
    for (size_t index = 0; index < buffers_.size(); ++index) {
      if (index < buffers_to_retain_.value() ||
          buffers_[index].ptr()->dirty()) {
        retained_buffers.push_back(std::move(buffers_[index]));
      } else {
        buffers_[index].ptr()->Close();
      }
    }
    buffers_ = std::move(retained_buffers);
  }

  // This is a copy of buffers_ but filtering down some buffers that shouldn't
  // be shown.
  std::vector<gc::Root<OpenBuffer>> buffers;
  std::optional<gc::Root<OpenBuffer>> active_buffer =
      active_buffer_widget_->Lock();
  size_t index_active = 0;  // The index in `buffers` of `active_buffer`.
  for (size_t index = 0; index < BuffersCount(); index++) {
    gc::Root<OpenBuffer> buffer = GetBuffer(index);
    if (active_buffer.has_value() &&
        &buffer.ptr().value() == &active_buffer->ptr().value()) {
      index_active = buffers.size();
    }
    buffers.push_back(std::move(buffer));
  }

  if (!buffers_to_show_.has_value()) {
    // Pass.
  } else if (buffers_to_show_.value() <= index_active) {
    gc::Root<OpenBuffer> buffer = std::move(buffers[index_active]);
    buffers.clear();
    buffers.push_back(std::move(buffer));
    index_active = 0;
  } else if (*buffers_to_show_ < buffers.size()) {
    buffers.erase(buffers.begin() + *buffers_to_show_, buffers.end());
  }

  if (buffers.empty()) {
    NonNull<std::unique_ptr<BufferWidget>> buffer_widget =
        MakeNonNullUnique<BufferWidget>(BufferWidget::Options{});
    active_buffer_widget_ = buffer_widget.get();
    widget_ = std::move(buffer_widget);
    return;
  }

  std::vector<NonNull<std::unique_ptr<BufferWidget>>> widgets;
  widgets.reserve(buffers.size());
  for (auto& buffer : buffers) {
    widgets.push_back(MakeNonNullUnique<BufferWidget>(BufferWidget::Options{
        .buffer = buffer.ptr().ToWeakPtr(),
        .is_active = widgets.size() == index_active ||
                     customer_->multiple_buffers_mode(),
        .position_in_parent =
            (buffers.size() > 1 ? widgets.size() : std::optional<size_t>())}));
  }

  CHECK_LT(index_active, widgets.size());
  active_buffer_widget_ = widgets[index_active].get();

  if (widgets.size() == 1) {
    widget_ = std::move(widgets[index_active]);
  } else {
    std::vector<NonNull<std::unique_ptr<Widget>>> widgets_base;
    for (auto& widget : widgets) widgets_base.push_back(std::move(widget));
    widget_ = MakeNonNullUnique<WidgetListHorizontal>(std::move(widgets_base),
                                                      index_active);
  }
}
}  // namespace afc::editor
