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
#include "src/time.h"
#include "src/widget.h"

namespace afc {
namespace editor {

namespace {
class BuffersListProducer : public OutputProducer {
 public:
  BuffersListProducer(std::map<wstring, std::shared_ptr<OpenBuffer>>* buffers,
                      std::shared_ptr<OpenBuffer> active_buffer_,
                      size_t buffers_per_line, ColumnNumberDelta width)
      : buffers_([&]() {
          std::vector<OpenBuffer*> output;
          for (const auto& it : *buffers) {
            output.push_back(it.second.get());
          }
          return output;
        }()),
        active_buffer_(std::move(active_buffer_)),
        buffers_per_line_(buffers_per_line),
        prefix_width_(max(2ul, std::to_wstring(buffers_.size()).size()) + 2),
        columns_per_buffer_(
            (width - std::min(width, (prefix_width_ * buffers_per_line_))) /
            buffers_per_line_),
        buffers_iterator_(buffers->begin()) {
    VLOG(1) << "BuffersList created. Buffers per line: " << buffers_per_line
            << ", prefix width: " << prefix_width_
            << ", count: " << buffers->size();
  }

  Generator Next() override {
    VLOG(2) << "BuffersListProducer::WriteLine start.";
    Generator output{
        std::nullopt, [this, index = index_]() {
          CHECK_LT(index, buffers_.size());
          Line::Options output;
          for (size_t i = 0;
               i < buffers_per_line_ && index + i < buffers_.size(); i++) {
            auto buffer = buffers_[index + i];
            auto name = buffer->Read(buffer_variables::name);
            auto number_prefix = std::to_wstring(index + i + 1);
            ColumnNumber start =
                ColumnNumber(0) + (columns_per_buffer_ + prefix_width_) * i;
            output.AppendString(
                ColumnNumberDelta::PaddingString(
                    start.ToDelta() - output.contents->size(), L' '),
                LineModifierSet());

            LineModifierSet number_modifiers;

            if (buffer->child_pid() != -1) {
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
              number_modifiers.insert(LineModifier::CYAN);
            }

            if (buffer == active_buffer_.get()) {
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

            std::list<std::wstring> output_components;
            std::list<std::wstring> components;
            if (buffer != nullptr &&
                buffer->Read(buffer_variables::path) == name &&
                DirectorySplit(name, &components) && !components.empty()) {
              name.clear();
              output_components.push_front(components.back());
              if (ColumnNumberDelta(output_components.front().size()) >
                  columns_per_buffer_) {
                output_components.front() = output_components.front().substr(
                    output_components.front().size() -
                    columns_per_buffer_.column_delta);
              } else {
                size_t consumed = output_components.front().size();
                components.pop_back();

                static const size_t kSizeOfSlash = 1;
                while (!components.empty()) {
                  if (columns_per_buffer_ >
                      ColumnNumberDelta(components.size() * 2 +
                                        components.back().size() + consumed)) {
                    output_components.push_front(components.back());
                    consumed += components.back().size() + kSizeOfSlash;
                  } else if (columns_per_buffer_ >
                             ColumnNumberDelta(1 + kSizeOfSlash + consumed)) {
                    output_components.push_front(
                        std::wstring(1, components.back()[0]));
                    consumed += 1 + kSizeOfSlash;
                  } else {
                    break;
                  }
                  components.pop_back();
                }
              }
            }

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

            output.AppendString(NewLazyString(progress), progress_modifier);
            if (!name.empty()) {
              std::shared_ptr<LazyString> output_name =
                  NewLazyString(std::move(name));
              if (output_name->size() > ColumnNumberDelta(2) &&
                  output_name->get(ColumnNumber(0)) == L'$' &&
                  output_name->get(ColumnNumber(1)) == L' ') {
                output_name = StringTrimLeft(
                    Substring(std::move(output_name), ColumnNumber(1)), L" ");
              }
              output.AppendString(SubstringWithRangeChecks(
                                      std::move(output_name), ColumnNumber(0),
                                      columns_per_buffer_),
                                  LineModifierSet());
              continue;
            }

            auto last = output_components.end();
            bool first_item = true;
            --last;
            for (auto it = output_components.begin();
                 it != output_components.end(); ++it) {
              if (!first_item) {
                output.AppendCharacter(L'/', {LineModifier::DIM});
              }
              first_item = false;
              output.AppendString(NewLazyString(std::move(*it)),
                                  it == last
                                      ? LineModifierSet{LineModifier::BOLD}
                                      : LineModifierSet({}));
            }
          }
          return LineWithCursor{std::make_shared<Line>(std::move(output)),
                                std::nullopt};
        }};
    index_ += buffers_per_line_;
    return output;
  }

 private:
  const std::vector<OpenBuffer*> buffers_;
  const std::shared_ptr<OpenBuffer> active_buffer_;
  const size_t buffers_per_line_;
  const ColumnNumberDelta prefix_width_;
  const ColumnNumberDelta columns_per_buffer_;

  int line_ = 0;
  std::map<wstring, std::shared_ptr<OpenBuffer>>::iterator buffers_iterator_;
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

std::unique_ptr<OutputProducer> BuffersList::CreateOutputProducer() {
  CHECK(widget_ != nullptr);
  auto output = widget_->CreateOutputProducer();
  if (buffers_list_lines_ == LineNumberDelta(0)) {
    return output;
  }

  std::vector<HorizontalSplitOutputProducer::Row> rows;
  rows.push_back({std::move(output), size_.line - buffers_list_lines_});

  if (buffers_list_lines_ > LineNumberDelta(0)) {
    rows.push_back({std::make_unique<BuffersListProducer>(
                        &buffers_, widget_->GetActiveLeaf()->Lock(),
                        buffers_per_line_, size_.column),
                    buffers_list_lines_});
  }

  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows), 0);
}

void BuffersList::SetSize(LineColumnDelta size) {
  size_ = size;

  static const auto kMinimumColumnsPerBuffer = ColumnNumberDelta(20);

  buffers_list_lines_ = LineNumberDelta(
      ceil(static_cast<double>(
               (buffers_.size() * kMinimumColumnsPerBuffer).column_delta) /
           size_.column.column_delta));
  buffers_per_line_ = ceil(static_cast<double>(buffers_.size()) /
                           buffers_list_lines_.line_delta);

  widget_->SetSize(
      LineColumnDelta(size_.line - buffers_list_lines_, size_.column));
}

LineColumnDelta BuffersList::size() const { return size_; }
LineNumberDelta BuffersList::MinimumLines() { return LineNumberDelta(0); }

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

}  // namespace editor
}  // namespace afc
