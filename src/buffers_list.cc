#include "buffers_list.h"

#include <algorithm>
#include <ctgmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/horizontal_split_output_producer.h"
#include "src/lazy_string_append.h"
#include "src/lazy_string_trim.h"
#include "src/tests/tests.h"
#include "src/time.h"
#include "src/widget.h"

namespace afc::editor {
namespace {
std::list<std::wstring> GetOutputComponents(
    const std::wstring& name, ColumnNumberDelta columns_per_buffer) {
  std::list<std::wstring> components;
  if (!DirectorySplit(name, &components) || components.empty()) {
    return {};
  }
  std::list<std::wstring> output;
  output.push_front(components.back());
  if (ColumnNumberDelta(output.front().size()) > columns_per_buffer) {
    output.front() = output.front().substr(output.front().size() -
                                           columns_per_buffer.column_delta);
  } else {
    size_t consumed = output.front().size();
    components.pop_back();

    static const size_t kSizeOfSlash = 1;
    while (!components.empty()) {
      if (columns_per_buffer >
          ColumnNumberDelta(components.size() * 2 + components.back().size() +
                            consumed)) {
        output.push_front(components.back());
        consumed += components.back().size() + kSizeOfSlash;
      } else if (columns_per_buffer >
                 ColumnNumberDelta(1 + kSizeOfSlash + consumed)) {
        output.push_front(std::wstring(1, components.back()[0]));
        consumed += 1 + kSizeOfSlash;
      } else {
        break;
      }
      components.pop_back();
    }
  }
  return output;
}

// Converts the `std::vector` of `BuffersList::filter_` to an
// `std::unordered_set` for faster access. For convenience, also takes care of
// resolving the `weak_ptr` buffers to their actual addresses (skipping expired
// buffers).
std::optional<std::unordered_set<OpenBuffer*>> OptimizeFilter(
    const std::optional<std::vector<std::weak_ptr<OpenBuffer>>> input) {
  if (!input.has_value()) {
    return std::nullopt;
  }

  std::unordered_set<OpenBuffer*> output;
  for (auto& weak_buffer : input.value()) {
    if (auto buffer = weak_buffer.lock(); buffer != nullptr) {
      output.insert(buffer.get());
    }
  }
  return output;
}

struct BuffersListOptions {
  const std::vector<std::shared_ptr<OpenBuffer>>* buffers;
  std::shared_ptr<OpenBuffer> active_buffer;
  std::set<OpenBuffer*> active_buffers;
  size_t buffers_per_line;
  ColumnNumberDelta width;
  std::optional<std::unordered_set<OpenBuffer*>> filter;
};

enum class FilterResult { kExcluded, kIncluded };

LineModifierSet GetNumberModifiers(const BuffersListOptions& options,
                                   OpenBuffer* buffer,
                                   FilterResult filter_result) {
  LineModifierSet output;
  if (buffer->status()->GetType() == Status::Type::kWarning) {
    output.insert(LineModifier::RED);
    const double kSecondsWarningHighlight = 5;
    if (GetElapsedSecondsSince(buffer->status()->last_change_time()) <
        kSecondsWarningHighlight) {
      output.insert(LineModifier::REVERSE);
    }
  } else if (filter_result == FilterResult::kExcluded) {
    output.insert(LineModifier::DIM);
  } else if (buffer->child_pid() != -1) {
    output.insert(LineModifier::YELLOW);
  } else if (buffer->child_exit_status().has_value()) {
    auto status = buffer->child_exit_status().value();
    if (!WIFEXITED(status)) {
      output.insert(LineModifier::RED);
      output.insert(LineModifier::BOLD);
    } else if (WEXITSTATUS(status) == 0) {
      output.insert(LineModifier::GREEN);
    } else {
      output.insert(LineModifier::RED);
    }
    if (GetElapsedSecondsSince(buffer->time_last_exit()) < 5.0) {
      output.insert({LineModifier::REVERSE});
    }
  } else {
    if (buffer->dirty()) {
      output.insert(LineModifier::ITALIC);
    }
    output.insert(LineModifier::CYAN);
  }
  if (buffer == options.active_buffer.get()) {
    output.insert(LineModifier::BOLD);
    output.insert(LineModifier::REVERSE);
  }
  return output;
}

class BuffersListProducer : public OutputProducer {
 public:
  BuffersListProducer(BuffersListOptions options)
      : options_(std::move(options)),
        prefix_width_(
            max(2ul, std::to_wstring(options_.buffers->size()).size()) + 2),
        columns_per_buffer_(
            (options_.width -
             std::min(options_.width,
                      (prefix_width_ * options_.buffers_per_line))) /
            options_.buffers_per_line),
        buffers_iterator_(options_.buffers->cbegin()) {
    VLOG(1) << "BuffersList created. Buffers per line: "
            << options_.buffers_per_line << ", prefix width: " << prefix_width_
            << ", count: " << options_.buffers->size();
  }

  Generator Next() override {
    VLOG(2) << "BuffersListProducer::WriteLine start, index: " << index_
            << ", buffers_per_line: " << options_.buffers_per_line
            << ", size: " << options_.buffers->size();

    Generator output{
        std::nullopt, [this, index = index_]() {
          CHECK_LT(index, options_.buffers->size())
              << "Buffers per line: " << options_.buffers_per_line;
          Line::Options output;
          for (size_t i = 0; i < options_.buffers_per_line &&
                             index + i < options_.buffers->size();
               i++) {
            auto buffer = buffers_iterator_->get();
            ++buffers_iterator_;
            auto number_prefix = std::to_wstring(index + i + 1);
            ColumnNumber start =
                ColumnNumber(0) + (columns_per_buffer_ + prefix_width_) * i;
            output.AppendString(
                ColumnNumberDelta::PaddingString(
                    start.ToDelta() - output.contents->size(), L' '),
                LineModifierSet());

            FilterResult filter_result =
                (!options_.filter.has_value() ||
                 options_.filter.value().find(buffer) !=
                     options_.filter.value().end())
                    ? FilterResult::kIncluded
                    : FilterResult::kExcluded;

            LineModifierSet number_modifiers =
                GetNumberModifiers(options_, buffer, filter_result);

            start +=
                prefix_width_ - ColumnNumberDelta(number_prefix.size() + 2);
            output.AppendString(
                StringAppend(
                    ColumnNumberDelta::PaddingString(
                        start.ToDelta() - output.contents->size(), L' '),
                    NewLazyString(number_prefix)),
                number_modifiers);

            wstring progress;
            LineModifierSet progress_modifier;
            if (!buffer->GetLineMarks()->empty()) {
              progress = L"!";
              progress_modifier.insert(LineModifier::RED);
            } else if (buffer->ShouldDisplayProgress()) {
              progress =
                  ProgressString(buffer->Read(buffer_variables::progress),
                                 OverflowBehavior::kModulo);
            } else {
              progress = ProgressStringFillUp(buffer->lines_size().line_delta,
                                              OverflowBehavior::kModulo);
              progress_modifier.insert(LineModifier::DIM);
            }
            // If we ever make ProgressString return more than a single
            // character, we'll have to adjust this.
            CHECK_LE(progress.size(), 1ul);

            output.AppendString(NewLazyString(progress),
                                filter_result == FilterResult::kExcluded
                                    ? LineModifierSet{LineModifier::DIM}
                                    : progress_modifier);
            SelectionState selection_state;
            switch (filter_result) {
              case FilterResult::kExcluded:
                selection_state = SelectionState::kExcludedByFilter;
                break;
              case FilterResult::kIncluded:
                selection_state = options_.active_buffers.find(buffer) !=
                                          options_.active_buffers.end()
                                      ? SelectionState::kReceivingInput
                                      : SelectionState::kIdle;
            }
            AppendBufferPath(columns_per_buffer_, *buffer,
                             buffer->dirty()
                                 ? LineModifierSet{LineModifier::ITALIC}
                                 : LineModifierSet{},
                             selection_state, &output);
          }
          return LineWithCursor{std::make_shared<Line>(std::move(output)),
                                std::nullopt};
        }};
    index_ += options_.buffers_per_line;
    return output;
  }

 private:
  enum class SelectionState { kReceivingInput, kIdle, kExcludedByFilter };
  static void AppendBufferPath(ColumnNumberDelta columns,
                               const OpenBuffer& buffer,
                               LineModifierSet modifiers,
                               SelectionState selection_state,
                               Line::Options* output) {
    std::optional<LineModifierSet> modifiers_override;

    LineModifierSet dim = modifiers;
    dim.insert(LineModifier::DIM);

    LineModifierSet bold = modifiers;

    switch (selection_state) {
      case SelectionState::kExcludedByFilter:
        modifiers.insert(LineModifier::DIM);
        bold.insert(LineModifier::DIM);
        break;
      case SelectionState::kReceivingInput:
        modifiers.insert(LineModifier::REVERSE);
        modifiers.insert(LineModifier::CYAN);
        bold = dim = modifiers;
        bold.insert(LineModifier::BOLD);
        break;
      case SelectionState::kIdle:
        bold.insert(LineModifier::BOLD);
        break;
    }

    std::list<std::wstring> components;
    auto name = buffer.Read(buffer_variables::name);

    if (buffer.Read(buffer_variables::path) == name) {
      components = GetOutputComponents(name, columns);
    }
    if (components.empty()) {
      std::shared_ptr<LazyString> output_name = NewLazyString(std::move(name));
      if (output_name->size() > ColumnNumberDelta(2) &&
          output_name->get(ColumnNumber(0)) == L'$' &&
          output_name->get(ColumnNumber(1)) == L' ') {
        output_name = StringTrimLeft(
            Substring(std::move(output_name), ColumnNumber(1)), L" ");
      }
      output->AppendString(SubstringWithRangeChecks(std::move(output_name),
                                                    ColumnNumber(0), columns),
                           modifiers);
      return;
    }

    for (auto it = components.begin(); it != components.end(); ++it) {
      if (it != components.begin()) {
        output->AppendCharacter(L'/', dim);
      }
      if (it != std::prev(components.end())) {
        output->AppendString(NewLazyString(std::move(*it)), modifiers);
        continue;
      }
      auto split = SplitExtension(*it);
      output->AppendString(split.prefix, bold);
      if (split.suffix.has_value()) {
        output->AppendString(split.suffix->separator, dim);
        output->AppendString(split.suffix->extension, bold);
      }
    }
  }

  const BuffersListOptions options_;
  const ColumnNumberDelta prefix_width_;
  const ColumnNumberDelta columns_per_buffer_;

  std::vector<std::shared_ptr<OpenBuffer>>::const_iterator buffers_iterator_;
  size_t index_ = 0;
};
}  // namespace

BuffersList::BuffersList(const EditorState* editor_state)
    : editor_state_(editor_state),
      widget_(std::make_unique<BufferWidget>(BufferWidget::Options{})),
      active_buffer_widget_(static_cast<BufferWidget*>(widget_.get())) {}

void BuffersList::AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                            AddBufferType add_buffer_type) {
  CHECK(buffer != nullptr);
  switch (add_buffer_type) {
    case AddBufferType::kVisit:
      if (std::find(buffers_.begin(), buffers_.end(), buffer) ==
          buffers_.end()) {
        buffers_.push_back(buffer);
      }
      active_buffer_widget_->SetBuffer(buffer);
      buffer->Visit();
      Update();
      break;

    case AddBufferType::kOnlyList:
      if (std::find(buffers_.begin(), buffers_.end(), buffer) ==
          buffers_.end()) {
        buffers_.push_back(buffer);
      }
      Update();
      break;

    case AddBufferType::kIgnore:
      break;
  }
}

std::vector<std::shared_ptr<OpenBuffer>> BuffersList::GetAllBuffers() const {
  return buffers_;
}

void BuffersList::RemoveBuffer(OpenBuffer* buffer) {
  CHECK(buffer != nullptr);
  if (auto it = std::find_if(buffers_.begin(), buffers_.end(),
                             [buffer](std::shared_ptr<OpenBuffer>& candidate) {
                               return candidate.get() == buffer;
                             });
      it != buffers_.end()) {
    buffers_.erase(it);
  }
  Update();
}

std::shared_ptr<OpenBuffer> BuffersList::GetBuffer(size_t index) {
  CHECK_LT(index, buffers_.size());
  return buffers_[index];
}

std::optional<size_t> BuffersList::GetBufferIndex(
    const OpenBuffer* buffer) const {
  auto it =
      std::find_if(buffers_.begin(), buffers_.end(),
                   [buffer](const std::shared_ptr<OpenBuffer>& candidate) {
                     return candidate.get() == buffer;
                   });
  return it == buffers_.end() ? std::optional<size_t>()
                              : std::distance(buffers_.begin(), it);
}

size_t BuffersList::GetCurrentIndex() {
  auto buffer = active_buffer();
  if (buffer == nullptr) {
    return 0;
  }
  return GetBufferIndex(buffer.get()).value();
}

size_t BuffersList::BuffersCount() const { return buffers_.size(); }

void BuffersList::set_filter(
    std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter) {
  filter_ = std::move(filter);
}

BufferWidget* BuffersList::GetActiveLeaf() {
  return const_cast<BufferWidget*>(
      const_cast<const BuffersList*>(this)->GetActiveLeaf());
}

const BufferWidget* BuffersList::GetActiveLeaf() const {
  return active_buffer_widget_;
}

namespace {
struct Layout {
  size_t buffers_per_line;
  LineNumberDelta lines;
};

Layout BuffersPerLine(LineNumberDelta maximum_lines, ColumnNumberDelta width,
                      size_t buffers_count) {
  if (buffers_count == 0 || maximum_lines.IsZero()) {
    return Layout{.buffers_per_line = 0, .lines = LineNumberDelta(0)};
  }
  double count = buffers_count;
  static const auto kDesiredColumnsPerBuffer = ColumnNumberDelta(20);
  size_t max_buffers_per_line =
      width /
      min(kDesiredColumnsPerBuffer,
          width / static_cast<size_t>(ceil(count / maximum_lines.line_delta)));
  auto lines = LineNumberDelta(ceil(count / max_buffers_per_line));
  return Layout{
      .buffers_per_line = static_cast<size_t>(ceil(count / lines.line_delta)),
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

std::unique_ptr<OutputProducer> BuffersList::CreateOutputProducer(
    Widget::OutputProducerOptions options) const {
  auto layout = BuffersPerLine(options.size.line / 2, options.size.column,
                               buffers_.size());
  VLOG(1) << "Buffers list lines: " << layout.lines
          << ", size: " << buffers_.size()
          << ", size column: " << options.size.column;

  options.size.line -= layout.lines;
  CHECK(widget_ != nullptr);
  auto output = widget_->CreateOutputProducer(options);
  if (layout.lines.IsZero()) return output;

  std::vector<HorizontalSplitOutputProducer::Row> rows;
  rows.push_back({std::move(output), options.size.line});

  if (layout.lines > LineNumberDelta(0)) {
    VLOG(2) << "Buffers per line: " << layout.buffers_per_line
            << ", from: " << buffers_.size()
            << " buffers with lines: " << layout.lines;

    std::set<OpenBuffer*> active_buffers;
    for (auto& b : editor_state_->active_buffers()) {
      active_buffers.insert(b.get());
    }
    rows.push_back({std::make_unique<BuffersListProducer>(BuffersListOptions{
                        .buffers = &buffers_,
                        .active_buffer = active_buffer(),
                        .active_buffers = std::move(active_buffers),
                        .buffers_per_line = layout.buffers_per_line,
                        .width = options.size.column,
                        .filter = OptimizeFilter(filter_)}),
                    layout.lines});
  }

  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows), 0);
}

std::shared_ptr<OpenBuffer> BuffersList::active_buffer() const {
  CHECK(active_buffer_widget_ != nullptr);
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
  auto order_predicate =
      buffer_sort_order_ == BufferSortOrder::kLastVisit
          ? [](const std::shared_ptr<OpenBuffer>& a,
               const std::shared_ptr<OpenBuffer>&
                   b) { return a->last_visit() > b->last_visit(); }
          : [](const std::shared_ptr<OpenBuffer>& a,
               const std::shared_ptr<OpenBuffer>& b) {
              return a->Read(buffer_variables::name) <
                     b->Read(buffer_variables::name);
            };
  std::sort(
      buffers_.begin(), buffers_.end(),
      [order_predicate](const std::shared_ptr<OpenBuffer>& a,
                        const std::shared_ptr<OpenBuffer>& b) {
        return (a->Read(buffer_variables::pin) == b->Read(buffer_variables::pin)
                    ? order_predicate(a, b)
                    : a->Read(buffer_variables::pin) >
                          b->Read(buffer_variables::pin));
      });

  if (buffers_to_retain_.has_value() &&
      buffers_.size() > buffers_to_retain_.value()) {
    std::vector<std::shared_ptr<OpenBuffer>> retained_buffers;
    retained_buffers.reserve(buffers_.size());
    for (size_t index = 0; index < buffers_.size(); ++index) {
      if (index < buffers_to_retain_.value() || buffers_[index]->dirty()) {
        retained_buffers.push_back(std::move(buffers_[index]));
      } else {
        buffers_[index]->Close();
      }
    }
    buffers_ = std::move(retained_buffers);
  }

  // This is a copy of buffers_ but filtering down some buffers that shouldn't
  // be shown.
  std::vector<std::shared_ptr<OpenBuffer>> buffers;
  auto active_buffer = active_buffer_widget_->Lock();
  size_t index_active = 0;  // The index in `buffers` of `active_buffer`.
  for (size_t index = 0; index < BuffersCount(); index++) {
    if (auto buffer = GetBuffer(index); buffer != nullptr) {
      if (buffer == active_buffer) {
        index_active = buffers.size();
      }
      buffers.push_back(std::move(buffer));
    }
  }

  if (!buffers_to_show_.has_value()) {
    // Pass.
  } else if (buffers_to_show_.value() <= index_active) {
    auto active_buffer = std::move(buffers[index_active]);
    buffers[0] = active_buffer;
    index_active = 0;
    buffers.resize(1);
  } else if (buffers_to_show_.value() < buffers.size()) {
    buffers.resize(buffers_to_show_.value());
  }

  if (buffers.empty()) {
    widget_ = std::make_unique<BufferWidget>(BufferWidget::Options{});
    active_buffer_widget_ = static_cast<BufferWidget*>(widget_.get());
    return;
  }

  std::vector<std::unique_ptr<Widget>> widgets;
  widgets.reserve(buffers.size());
  for (auto& buffer : buffers) {
    widgets.push_back(std::make_unique<BufferWidget>(BufferWidget::Options{
        .buffer = buffer,
        .is_active = widgets.size() == index_active ||
                     editor_state_->Read(editor_variables::multiple_buffers),
        .position_in_parent =
            (buffers.size() > 1 ? widgets.size() : std::optional<size_t>())}));
  }

  CHECK_LT(index_active, widgets.size());
  active_buffer_widget_ =
      static_cast<BufferWidget*>(widgets[index_active].get());

  if (widgets.size() == 1) {
    widget_ = std::move(widgets[index_active]);
  } else {
    widget_ = std::make_unique<WidgetListHorizontal>(
        editor_state_, std::move(widgets), index_active);
  }
}
}  // namespace afc::editor
