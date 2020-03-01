#include "buffers_list.h"

#include <algorithm>
#include <ctgmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
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
  const std::map<wstring, std::shared_ptr<OpenBuffer>>* buffers;
  std::shared_ptr<OpenBuffer> active_buffer;
  size_t buffers_per_line;
  ColumnNumberDelta width;
  std::optional<std::unordered_set<OpenBuffer*>> filter;
};

enum class FilterResult { kExcluded, kIncluded };

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
            auto buffer = buffers_iterator_->second.get();
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

            LineModifierSet number_modifiers;

            if (filter_result == FilterResult::kExcluded) {
              number_modifiers.insert(LineModifier::DIM);
            } else if (buffer->child_pid() != -1) {
              number_modifiers.insert(LineModifier::YELLOW);
            } else if (buffer->child_exit_status().has_value()) {
              auto status = buffer->child_exit_status().value();
              if (!WIFEXITED(status)) {
                number_modifiers.insert(LineModifier::RED);
                number_modifiers.insert(LineModifier::BOLD);
              } else if (WEXITSTATUS(status) == 0) {
                number_modifiers.insert(LineModifier::GREEN);
              } else {
                number_modifiers.insert(LineModifier::RED);
              }
              if (GetElapsedSecondsSince(buffer->time_last_exit()) < 5.0) {
                number_modifiers.insert({LineModifier::REVERSE});
              }
            } else {
              if (buffer->dirty()) {
                number_modifiers.insert(LineModifier::ITALIC);
              }
              number_modifiers.insert(LineModifier::CYAN);
            }

            if (buffer == options_.active_buffer.get()) {
              number_modifiers.insert(LineModifier::BOLD);
              number_modifiers.insert(LineModifier::REVERSE);
            }
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
            AppendBufferPath(columns_per_buffer_, *buffer,
                             buffer->dirty()
                                 ? LineModifierSet{LineModifier::ITALIC}
                                 : LineModifierSet{},
                             filter_result, &output);
          }
          return LineWithCursor{std::make_shared<Line>(std::move(output)),
                                std::nullopt};
        }};
    index_ += options_.buffers_per_line;
    return output;
  }

 private:
  static void AppendBufferPath(ColumnNumberDelta columns,
                               const OpenBuffer& buffer,
                               LineModifierSet modifiers,
                               FilterResult filter_result,
                               Line::Options* output) {
    LineModifierSet dim = modifiers;
    dim.insert(LineModifier::DIM);

    LineModifierSet bold = modifiers;

    switch (filter_result) {
      case FilterResult::kExcluded:
        modifiers.insert(LineModifier::DIM);
        bold.insert(LineModifier::DIM);
        break;
      case FilterResult::kIncluded:
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

  std::map<wstring, std::shared_ptr<OpenBuffer>>::const_iterator
      buffers_iterator_;
  size_t index_ = 0;
};
}  // namespace

BuffersList::BuffersList(std::unique_ptr<Widget> widget)
    : widget_(std::move(widget)) {
  CHECK(widget_ != nullptr);
}

void BuffersList::AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                            AddBufferType add_buffer_type) {
  CHECK(buffer != nullptr);
  switch (add_buffer_type) {
    case AddBufferType::kVisit:
      buffers_[buffer->Read(buffer_variables::name)] = buffer;
      GetActiveLeaf()->SetBuffer(buffer);
      break;

    case AddBufferType::kOnlyList:
      buffers_[buffer->Read(buffer_variables::name)] = buffer;
      break;

    case AddBufferType::kIgnore:
      break;
  }
}

void BuffersList::RemoveBuffer(OpenBuffer* buffer) {
  CHECK(widget_ != nullptr);
  CHECK(buffer != nullptr);
  buffers_.erase(buffer->Read(buffer_variables::name));
  CHECK(widget_ != nullptr);
  widget_->RemoveBuffer(buffer);
}

size_t BuffersList::CountLeaves() const { return widget_->CountLeaves(); }

int BuffersList::AdvanceActiveLeafWithoutWrapping(int delta) {
  CHECK(widget_ != nullptr);
  return widget_->AdvanceActiveLeafWithoutWrapping(delta);
}

void BuffersList::SetActiveLeavesAtStart() {
  CHECK(widget_ != nullptr);
  widget_->SetActiveLeavesAtStart();
}

std::shared_ptr<OpenBuffer> BuffersList::GetBuffer(size_t index) {
  CHECK_LT(index, buffers_.size());
  auto it = buffers_.begin();
  std::advance(it, index);
  return it->second;
}

std::optional<size_t> BuffersList::GetBufferIndex(
    const OpenBuffer* buffer) const {
  auto it = buffers_.find(buffer->Read(buffer_variables::name));
  return it == buffers_.end() ? std::optional<size_t>()
                              : std::distance(buffers_.begin(), it);
}

size_t BuffersList::GetCurrentIndex() {
  auto buffer = GetActiveLeaf()->Lock();
  if (buffer == nullptr) {
    return 0;
  }
  auto it = buffers_.find(buffer->Read(buffer_variables::name));
  if (it == buffers_.end()) {
    return 0;
  }
  return std::distance(buffers_.begin(), it);
}

size_t BuffersList::BuffersCount() const { return buffers_.size(); }

void BuffersList::set_filter(
    std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter) {
  filter_ = std::move(filter);
}

wstring BuffersList::Name() const {
  CHECK(widget_ != nullptr);
  return widget_->Name();
}

wstring BuffersList::ToString() const {
  return L"BuffersList: " + widget_->Name();
}

BufferWidget* BuffersList::GetActiveLeaf() {
  return const_cast<BufferWidget*>(
      const_cast<const BuffersList*>(this)->GetActiveLeaf());
}

const BufferWidget* BuffersList::GetActiveLeaf() const {
  CHECK(widget_ != nullptr);
  return widget_->GetActiveLeaf();
}

void BuffersList::ForEachBufferWidget(
    std::function<void(BufferWidget*)> callback) {
  CHECK(widget_ != nullptr);
  widget_->ForEachBufferWidget(std::move(callback));
}

void BuffersList::ForEachBufferWidgetConst(
    std::function<void(const BufferWidget*)> callback) const {
  CHECK(widget_ != nullptr);
  widget_->ForEachBufferWidgetConst(callback);
}

namespace {
struct Layout {
  size_t buffers_per_line;
  LineNumberDelta lines;
};

Layout BuffersPerLine(ColumnNumberDelta width, size_t buffers_count) {
  double count = buffers_count;
  static const auto kMinimumColumnsPerBuffer = ColumnNumberDelta(20);
  size_t max_buffers_per_line = width / kMinimumColumnsPerBuffer;
  auto lines = LineNumberDelta(ceil(count / max_buffers_per_line));
  return Layout{
      .buffers_per_line = static_cast<size_t>(ceil(count / lines.line_delta)),
      .lines = lines};
}

class BuffersPerLineTests : public tests::TestGroup<BuffersPerLineTests> {
 public:
  std::wstring Name() const override { return L"BuffersPerLineTests"; }
  std::vector<tests::Test> Tests() const override {
    return {{.name = L"SingleBuffer",
             .callback =
                 [] {
                   auto layout = BuffersPerLine(ColumnNumberDelta(100), 1);
                   CHECK_EQ(layout.lines, LineNumberDelta(1));
                   CHECK_EQ(layout.buffers_per_line, 1ul);
                 }},
            {.name = L"SingleLine",
             .callback =
                 [] {
                   auto layout = BuffersPerLine(ColumnNumberDelta(100), 4);
                   CHECK_EQ(layout.lines, LineNumberDelta(1));
                   CHECK_EQ(layout.buffers_per_line, 4ul);
                 }},
            {.name = L"SingleLineFull",
             .callback =
                 [] {
                   auto layout = BuffersPerLine(ColumnNumberDelta(100), 5);
                   CHECK_EQ(layout.lines, LineNumberDelta(1));
                   CHECK_EQ(layout.buffers_per_line, 5ul);
                 }},
            {.name = L"TwoLinesJustAtBoundary",
             .callback =
                 [] {
                   // Identical to SingleLineFull, but the buffers don't fit by
                   // 1 position.
                   auto layout = BuffersPerLine(ColumnNumberDelta(99), 5);
                   CHECK_EQ(layout.lines, LineNumberDelta(2));
                   CHECK_EQ(layout.buffers_per_line, 3ul);
                 }},
            {.name = L"ThreeLines",
             .callback =
                 [] {
                   auto layout = BuffersPerLine(ColumnNumberDelta(100), 11);
                   CHECK_EQ(layout.lines, LineNumberDelta(3));
                   CHECK_EQ(layout.buffers_per_line, 4ul);
                 }},
            {.name = L"ManyLines", .callback = [] {
               auto layout = BuffersPerLine(ColumnNumberDelta(100), 250);
               CHECK_EQ(layout.lines, LineNumberDelta(50));
               CHECK_EQ(layout.buffers_per_line, 5ul);
             }}};
  }
};

template <>
const bool tests::TestGroup<BuffersPerLineTests>::registration_ =
    tests::Add<editor::BuffersPerLineTests>();
}  // namespace

std::unique_ptr<OutputProducer> BuffersList::CreateOutputProducer(
    OutputProducerOptions options) const {
  auto layout = BuffersPerLine(options.size.column, buffers_.size());
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

    rows.push_back({std::make_unique<BuffersListProducer>(BuffersListOptions{
                        .buffers = &buffers_,
                        .active_buffer = widget_->GetActiveLeaf()->Lock(),
                        .buffers_per_line = layout.buffers_per_line,
                        .width = options.size.column,
                        .filter = OptimizeFilter(filter_)}),
                    layout.lines});
  }

  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows), 0);
}

LineNumberDelta BuffersList::MinimumLines() const { return LineNumberDelta(0); }

Widget* BuffersList::Child() { return widget_.get(); }

void BuffersList::SetChild(std::unique_ptr<Widget> widget) {
  CHECK(widget != nullptr);
  widget_ = std::move(widget);
}

void BuffersList::WrapChild(
    std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)> callback) {
  widget_ = callback(std::move(widget_));
  CHECK(widget_ != nullptr);
}

}  // namespace afc::editor
