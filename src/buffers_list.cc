#include "buffers_list.h"

#include <algorithm>
#include <ctgmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/editor.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/overload.h"
#include "src/tests/tests.h"
#include "src/widget.h"

namespace afc::editor {
using infrastructure::GetElapsedSecondsSince;
using infrastructure::Path;
using infrastructure::PathComponent;
using infrastructure::Tracker;
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

namespace {
struct ProcessedPathComponent {
  PathComponent path_component;
  bool complete = true;

  bool operator==(const ProcessedPathComponent& other) const {
    return path_component == other.path_component && complete == other.complete;
  }
};

ValueOrError<std::list<ProcessedPathComponent>> GetOutputComponents(
    const std::list<PathComponent>& components,
    ColumnNumberDelta columns_per_buffer) {
  if (components.empty()) return Error(L"Empty components");

  static const std::wstring kSlash = L"/";
  static const ColumnNumberDelta kSlashSize(kSlash.size());
  std::list<ProcessedPathComponent> output;

  ColumnNumberDelta consumed;
  int components_processed = 0;
  for (auto it = components.rbegin();
       it != components.rend() &&
       (output.empty() || consumed + kSlashSize < columns_per_buffer);
       ++it, ++components_processed) {
    const size_t initial_size = it->size();
    ColumnNumberDelta columns_allowed =
        columns_per_buffer - consumed -
        (output.empty() ? ColumnNumberDelta() : kSlashSize);
    if (!output.empty() && std::next(it) != components.rend() &&
        columns_allowed > ColumnNumberDelta(2)) {
      columns_allowed = std::max(
          ColumnNumberDelta(1),
          columns_allowed - (ColumnNumberDelta(1) + kSlashSize) *
                                (components.size() - components_processed - 1));
    }
    ASSIGN_OR_RETURN(
        PathComponent next,
        ColumnNumberDelta(it->size()) <= columns_allowed
            ? *it
            : PathComponent::FromString(
                  output.empty()
                      ? it->ToString().substr(it->size() -
                                              columns_allowed.read())
                      : it->ToString().substr(0, columns_allowed.read())));
    consumed +=
        ColumnNumberDelta(next.size() + (output.empty() ? 0 : kSlash.size()));
    CHECK_LE(consumed, columns_per_buffer);
    bool complete = next.size() == initial_size;
    output.push_front(
        {.path_component = std::move(next), .complete = complete});
  }

  return output;
}

std::list<ProcessedPathComponent> GetOutputComponentsForTesting(
    std::wstring path, ColumnNumberDelta columns_per_buffer) {
  ValueOrError<std::list<PathComponent>> components =
      ValueOrDie(Path::FromString(path)).DirectorySplit();
  return ValueOrDie(
      GetOutputComponents(std::get<0>(components), columns_per_buffer));
}

const bool get_output_components_tests_registration = tests::Register(
    L"GetOutputComponentsTests",
    {{.name = L"SingleFits",
      .callback =
          [] {
            CHECK(
                GetOutputComponentsForTesting(L"foo", ColumnNumberDelta(80)) ==
                std::list<ProcessedPathComponent>(
                    {{.path_component =
                          ValueOrDie(PathComponent::FromString(L"foo"))}}));
          }},
     {.name = L"SingleTrim",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro",
                                                ColumnNumberDelta(7)) ==
                  std::list<ProcessedPathComponent>(
                      {{.path_component =
                            ValueOrDie(PathComponent::FromString(L"ejandro")),
                        .complete = false}}));
          }},
     {.name = L"SingleFitsExactly",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro",
                                                ColumnNumberDelta(9)) ==
                  std::list<ProcessedPathComponent>(
                      {{.path_component = ValueOrDie(
                            PathComponent::FromString(L"alejandro"))}}));
          }},
     {.name = L"MultipleFits",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(80)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"alejandro"))},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"forero"))},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleFitsExactly",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(23)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"alejandro"))},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"forero"))},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimFirst",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(22)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"alejandr")),
                       .complete = false},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"forero"))},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimSignificant",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(
                      L"alejandro/forero/cuervo",
                      ColumnNumberDelta((1 + 1) + (1 + 1) + 6)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"a")),
                       .complete = false},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"f")),
                       .complete = false},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimSpill",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(2 + 1 + 6)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"fo")),
                       .complete = false},
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimToFirst",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(5)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"uervo")),
                       .complete = false},
                  }));
          }},
     {.name = L"MultipleTrimExact",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(6)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimUnusedSpill",
      .callback =
          [] {
            CHECK(GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                                ColumnNumberDelta(7)) ==
                  std::list<ProcessedPathComponent>({
                      {.path_component =
                           ValueOrDie(PathComponent::FromString(L"cuervo"))},
                  }));
          }},
     {.name = L"MultipleTrimSmallSpill", .callback = [] {
        CHECK(
            GetOutputComponentsForTesting(L"alejandro/forero/cuervo",
                                          ColumnNumberDelta(8)) ==
            std::list<ProcessedPathComponent>({
                {.path_component = ValueOrDie(PathComponent::FromString(L"f")),
                 .complete = false},
                {.path_component =
                     ValueOrDie(PathComponent::FromString(L"cuervo"))},
            }));
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
  } else if (buffer.child_pid() != -1) {
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
  std::vector<std::list<PathComponent>> transformed;
  for (const std::wstring& c : input) {
    transformed.push_back(std::visit(
        overload{[](Error) { return std::list<PathComponent>(); },
                 [](Path path) {
                   return ValueOrDie(path.DirectorySplit(),
                                     L"RemoveCommonPrefixesForTesting");
                 }},
        Path::FromString(c)));
  }
  std::vector<std::wstring> output;
  for (auto& components : RemoveCommonPrefixes(transformed)) {
    std::optional<Path> path;
    for (auto& c : components)
      path = path.has_value() ? Path::Join(*path, c) : c;
    output.push_back(path.has_value() ? path->read() : L"");
  }
  return output;
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

LineBuilder GetBufferContents(const OpenBuffer& buffer,
                              ColumnNumberDelta columns) {
  NonNull<std::shared_ptr<const Line>> line =
      buffer.contents().at(LineNumber(0));
  LineBuilder output;
  if ((line->EndColumn() + ColumnNumberDelta(1)).ToDelta() < columns) {
    ColumnNumberDelta padding = (columns - line->EndColumn().ToDelta()) / 2;
    output.AppendString(Padding(padding, L' '));
  }

  output.Append(line->CopyLineBuilder().DeleteSuffix(ColumnNumber() + columns));
  output.ClearModifiers();
  output.InsertModifier(ColumnNumber{}, LineModifier::kDim);
  return output;
}

LineBuilder GetBufferVisibleString(
    ColumnNumberDelta columns, const OpenBuffer& buffer,
    LineModifierSet modifiers, SelectionState selection_state,
    const std::list<ProcessedPathComponent>& components) {
  LineBuilder output;
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

  std::wstring name = buffer.Read(buffer_variables::name);
  std::replace(name.begin(), name.end(), L'\n', L' ');
  if (components.empty()) {
    NonNull<std::shared_ptr<LazyString>> output_name =
        NewLazyString(std::move(name));
    if (output_name->size() > ColumnNumberDelta(2) &&
        output_name->get(ColumnNumber(0)) == L'$' &&
        output_name->get(ColumnNumber(1)) == L' ') {
      output_name =
          TrimLeft(Substring(std::move(output_name), ColumnNumber(1)), L" ");
    }
    output.AppendString(SubstringWithRangeChecks(std::move(output_name),
                                                 ColumnNumber(0), columns),
                        modifiers);
  } else {
    std::wstring separator;
    for (auto it = components.begin(); it != components.end(); ++it) {
      output.AppendString(separator, dim);
      columns -= ColumnNumberDelta(separator.size());
      separator = it->complete ? L"/" : L"…";
      if (it != std::prev(components.end())) {
        output.AppendString(
            NewLazyString(std::move(it->path_component.ToString())), modifiers);
        continue;
      }

      PathComponent base = OptionalFrom(it->path_component.remove_extension())
                               .value_or(it->path_component);
      std::optional<std::wstring> extension = it->path_component.extension();
      output.AppendString(base.ToString(), bold);
      if (extension.has_value()) {
        output.AppendString(L".", dim);
        output.AppendString(extension.value(), bold);
      }
    }
  }
  if (columns > output.EndColumn().ToDelta())
    output.Append(
        GetBufferContents(buffer, columns - output.EndColumn().ToDelta()));

  return output;
}

ValueOrError<std::list<PathComponent>> GetPathComponentsForBuffer(
    const OpenBuffer& buffer) {
  auto path_str = buffer.Read(buffer_variables::path);
  if (path_str != buffer.Read(buffer_variables::name)) {
    return Error(L"name doesn't match path.");
  }
  ASSIGN_OR_RETURN(Path path, Path::FromString(path_str));
  ASSIGN_OR_RETURN(std::list<PathComponent> components, path.DirectorySplit());
  return components;
}

LineWithCursor::Generator::Vector ProduceBuffersList(
    std::shared_ptr<BuffersListOptions> options) {
  static Tracker tracker(L"BuffersList::ProduceBuffersList");
  auto call = tracker.Call();

  const ColumnNumberDelta prefix_width = ColumnNumberDelta(
      std::max(2ul, std::to_wstring(options->buffers.size()).size()) + 2);

  const ColumnNumberDelta columns_per_buffer =
      (options->size.column -
       std::min(options->size.column,
                (prefix_width * options->buffers_per_line))) /
      options->buffers_per_line;

  // Contains one element for each entry in options.buffers.
  const std::vector<std::list<ProcessedPathComponent>> path_components = [&] {
    std::vector<std::list<PathComponent>> paths;
    for (const gc::Root<OpenBuffer>& buffer : options->buffers)
      paths.push_back(
          OptionalFrom(GetPathComponentsForBuffer(buffer.ptr().value()))
              .value_or(std::list<PathComponent>()));
    std::vector<std::list<ProcessedPathComponent>> output;
    for (const auto& path : RemoveCommonPrefixes(paths)) {
      output.push_back(
          path.empty()
              ? std::list<ProcessedPathComponent>({})
              : OptionalFrom(GetOutputComponents(path, columns_per_buffer))
                    .value_or(std::list<ProcessedPathComponent>({})));
    }
    CHECK_EQ(output.size(), options->buffers.size());
    return output;
  }();

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
        std::nullopt,
        [options, prefix_width, path_components, columns_per_buffer, index]() {
          LineBuilder line_options_output;
          for (size_t j = 0; j < options->buffers_per_line &&
                             index + j < options->buffers.size();
               j++) {
            const OpenBuffer& buffer =
                options->buffers.at(index + j).ptr().value();
            auto number_prefix = std::to_wstring(index + j + 1);
            ColumnNumber start =
                ColumnNumber(0) + (columns_per_buffer + prefix_width) * j;
            line_options_output.AppendString(
                Padding(
                    start.ToDelta() - line_options_output.contents()->size(),
                    L' '),
                LineModifierSet());

            FilterResult filter_result =
                (!options->filter.has_value() ||
                 options->filter.value().find(
                     NonNull<const OpenBuffer*>::AddressOf(buffer)) !=
                     options->filter.value().end())
                    ? FilterResult::kIncluded
                    : FilterResult::kExcluded;

            LineModifierSet number_modifiers =
                GetNumberModifiers(*options, buffer, filter_result);

            start += prefix_width - ColumnNumberDelta(number_prefix.size() + 2);
            line_options_output.AppendString(
                Append(Padding(start.ToDelta() -
                                   line_options_output.contents()->size(),
                               L' '),
                       NewLazyString(number_prefix)),
                number_modifiers);

            std::wstring progress;
            LineModifierSet progress_modifier;
            if (!buffer.GetLineMarks().empty()) {
              progress = L"!";
              progress_modifier.insert(LineModifier::kRed);
            } else if (!buffer.GetExpiredLineMarks().empty()) {
              progress = L"!";
            } else if (buffer.ShouldDisplayProgress()) {
              progress = ProgressString(buffer.Read(buffer_variables::progress),
                                        OverflowBehavior::kModulo);
            } else {
              progress = ProgressStringFillUp(buffer.lines_size().read(),
                                              OverflowBehavior::kModulo);
              progress_modifier.insert(LineModifier::kDim);
            }
            // If we ever make ProgressString return more than a single
            // character, we'll have to adjust this.
            CHECK_LE(progress.size(), 1ul);

            line_options_output.AppendString(
                NewLazyString(progress),
                filter_result == FilterResult::kExcluded
                    ? LineModifierSet{LineModifier::kDim}
                    : progress_modifier);
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
            line_options_output.Append(GetBufferVisibleString(
                columns_per_buffer, buffer,
                buffer.dirty() ? LineModifierSet{LineModifier::kItalic}
                               : LineModifierSet{},
                selection_state, path_components[index + j]));
          }
          return LineWithCursor{
              .line = MakeNonNullShared<Line>(std::move(line_options_output))};
        }});
    index += options->buffers_per_line;
  }
  return output;
}
}  // namespace

BuffersList::BuffersList(const EditorState& editor_state)
    : BuffersList(editor_state,
                  MakeNonNullUnique<BufferWidget>(BufferWidget::Options{})) {}

BuffersList::BuffersList(const EditorState& editor_state,
                         NonNull<std::unique_ptr<BufferWidget>> widget)
    : editor_state_(editor_state),
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

void BuffersList::RemoveBuffer(const OpenBuffer& buffer) {
  if (auto it = std::find_if(buffers_.begin(), buffers_.end(),
                             [&buffer](gc::Root<OpenBuffer>& candidate) {
                               return &candidate.ptr().value() == &buffer;
                             });
      it != buffers_.end()) {
    LOG(INFO) << "BuffersList: Removing buffer: " << buffer.name();
    buffers_.erase(it);
  }
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
  static Tracker tracker(L"BuffersPerLine");
  auto call = tracker.Call();

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
  static Tracker tracker(L"BuffersList::GetLines");
  auto call = tracker.Call();

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

    std::set<NonNull<const OpenBuffer*>> active_buffers;
    for (gc::Root<OpenBuffer>& b : editor_state_.active_buffers()) {
      active_buffers.insert(
          NonNull<const OpenBuffer*>::AddressOf(b.ptr().value()));
    }

    output = ProduceBuffersList(
        std::make_shared<BuffersListOptions>(BuffersListOptions{
            .buffers = buffers_,
            .active_buffer = active_buffer(),
            .active_buffers = std::move(active_buffers),
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
  static Tracker tracker(L"BuffersList::Update");
  auto call = tracker.Call();

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
    buffers = {std::move(buffers[index_active])};
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
                     editor_state_.Read(editor_variables::multiple_buffers),
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
