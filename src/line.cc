#include "src/line.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

using std::hash;
using std::unordered_set;
using std::wstring;

Line::Line(wstring x) : Line(Line::Options(NewLazyString(std::move(x)))) {}

Line::Line(const Options& options)
    : environment_(options.environment == nullptr
                       ? std::make_shared<Environment>()
                       : options.environment),
      contents_(options.contents),
      modifiers_(options.modifiers),
      options_(std::move(options)) {
  CHECK(contents_ != nullptr);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

Line::Line(const Line& line) {
  std::unique_lock<std::mutex> lock(line.mutex_);
  environment_ = line.environment_;
  contents_ = line.contents_;
  modifiers_ = line.modifiers_;
  options_ = line.options_;
}

shared_ptr<LazyString> Line::Substring(size_t pos, size_t length) const {
  return afc::editor::Substring(contents(), pos, length);
}

shared_ptr<LazyString> Line::Substring(size_t pos) const {
  return afc::editor::Substring(contents(), pos);
}

void Line::DeleteCharacters(size_t position, size_t amount) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_LE(position, contents_->size());
  CHECK_LE(position + amount, contents_->size());
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ =
      StringAppend(afc::editor::Substring(contents_, 0, position),
                   afc::editor::Substring(contents_, position + amount));
  auto it = modifiers_.begin() + position;
  modifiers_.erase(it, it + amount);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::DeleteCharacters(size_t position) {
  CHECK_LE(position, size());
  DeleteCharacters(position, size() - position);
}

void Line::InsertCharacterAtPosition(size_t position) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ =
      StringAppend(StringAppend(afc::editor::Substring(contents_, 0, position),
                                NewLazyString(L" ")),
                   afc::editor::Substring(contents_, position));

  modifiers_.push_back(unordered_set<LineModifier, hash<int>>());
  for (size_t i = modifiers_.size() - 1; i > position; i--) {
    modifiers_[i] = modifiers_[i - 1];
  }
}

void Line::SetCharacter(
    size_t position, int c,
    const unordered_set<LineModifier, hash<int>>& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  shared_ptr<LazyString> str = NewLazyString(wstring(1, c));
  if (position >= contents_->size()) {
    contents_ = StringAppend(contents_, str);
    modifiers_.push_back(modifiers);
  } else {
    contents_ = StringAppend(
        StringAppend(afc::editor::Substring(contents_, 0, position), str),
        afc::editor::Substring(contents_, position + 1));
    if (modifiers_.size() <= position) {
      modifiers_.resize(position + 1);
    }
    modifiers_[position] = modifiers;
  }
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::SetAllModifiers(const LineModifierSet& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  modifiers_.assign(contents_->size(), modifiers);
  options_.end_of_line_modifiers = modifiers;
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::Append(const Line& line) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  CHECK_EQ(line.contents_->size(), line.modifiers_.size());
  CHECK(this != &line);
  contents_ = StringAppend(contents_, line.contents_);
  for (auto& m : line.modifiers_) {
    modifiers_.push_back(m);
  }
  options_.end_of_line_modifiers = line.options_.end_of_line_modifiers;
  CHECK_EQ(contents_->size(), modifiers_.size());
}

std::shared_ptr<vm::Environment> Line::environment() const {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK(environment_ != nullptr);
  return environment_;
}

namespace {
void Draw(size_t pos, wchar_t padding_char, wchar_t final_char,
          wchar_t connect_final_char, wstring* output) {
  CHECK_LT(pos, output->size());
  for (size_t i = 0; i < pos; i++) {
    output->at(i) = padding_char;
  }
  output->at(pos) = (pos + 1 == output->size() || output->at(pos + 1) == L' ' ||
                     output->at(pos + 1) == L'│')
                        ? final_char
                        : connect_final_char;
}

wstring DrawTree(size_t line, size_t lines_size, const ParseTree& root) {
  // Route along the tree where each child ends after previous line.
  vector<const ParseTree*> route_begin;
  if (line > 0) {
    route_begin = MapRoute(
        root,
        FindRouteToPosition(
            root, LineColumn(line - 1, std::numeric_limits<size_t>::max())));
    CHECK(!route_begin.empty() && *route_begin.begin() == &root);
    route_begin.erase(route_begin.begin());
  }

  // Route along the tree where each child ends after current line.
  vector<const ParseTree*> route_end;
  if (line < lines_size - 1) {
    route_end = MapRoute(
        root, FindRouteToPosition(
                  root, LineColumn(line, std::numeric_limits<size_t>::max())));
    CHECK(!route_end.empty() && *route_end.begin() == &root);
    route_end.erase(route_end.begin());
  }

  wstring output(root.depth, L' ');
  size_t index_begin = 0;
  size_t index_end = 0;
  while (index_begin < route_begin.size() || index_end < route_end.size()) {
    if (index_begin == route_begin.size()) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth > route_end[index_end]->depth) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth > route_begin[index_begin]->depth) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth] = L'│';
      index_begin++;
      index_end++;
      continue;
    }

    Draw(route_end[index_end]->depth, L'─', L'┤', L'┼', &output);
    index_begin++;
    index_end++;
  }
  return output;
}

wchar_t ComputeScrollBarCharacter(size_t line, size_t lines_size,
                                  size_t view_start, size_t lines_to_show) {
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, view_start);
  DCHECK_LT(line - view_start, lines_to_show)
      << "Line is " << line << " and view_start is " << view_start
      << ", which exceeds lines_to_show of " << lines_to_show;
  DCHECK_LT(view_start, lines_size);
  size_t halves_to_show = lines_to_show * 2;

  // Number of halves the bar should take.
  size_t bar_size =
      max(size_t(1),
          size_t(std::round(halves_to_show *
                            static_cast<double>(lines_to_show) / lines_size)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start =
      std::round(halves_to_show * static_cast<double>(view_start) / lines_size);
  size_t end = start + bar_size;

  size_t current = 2 * (line - view_start);
  if (current < start - (start % 2) || current >= end) {
    return L' ';
  } else if (start == current + 1) {
    return L'▄';
  } else if (current + 1 == end) {
    return L'▀';
  } else {
    return L'█';
  }
}
}  // namespace

void Line::Output(const Line::OutputOptions& options) const {
  CHECK(options.editor_state != nullptr);
  CHECK(options.buffer != nullptr);
  std::unique_lock<std::mutex> lock(mutex_);
  VLOG(5) << "Producing output of line: " << ToString();
  size_t input_column = options.position.column;
  unordered_set<LineModifier, hash<int>> current_modifiers;

  CHECK(environment_ != nullptr);
  auto target_buffer_value =
      environment_->Lookup(L"buffer", vm::VMTypeMapper<OpenBuffer*>::vmtype);
  const auto target_buffer =
      (target_buffer_value != nullptr &&
       target_buffer_value->user_value != nullptr)
          ? static_cast<OpenBuffer*>(target_buffer_value->user_value.get())
          : options.buffer;
  const auto view_start_line =
      options.buffer->Read(buffer_variables::view_start_line());

  while (input_column < contents_->size() &&
         options.output_receiver->column() < options.width) {
    wint_t c = contents_->get(input_column);
    CHECK(c != '\n');
    // TODO: Optimize.
    if (input_column >= modifiers_.size()) {
      options.output_receiver->AddModifier(LineModifier::RESET);
    } else if (modifiers_[input_column] != current_modifiers) {
      options.output_receiver->AddModifier(LineModifier::RESET);
      current_modifiers = modifiers_[input_column];
      for (auto it : current_modifiers) {
        options.output_receiver->AddModifier(it);
      }
    }
    switch (c) {
      case L'\r':
        break;
      default:
        VLOG(8) << "Print character: " << c;
        options.output_receiver->AddCharacter(c);
    }
    input_column++;
  }
  options.output_receiver->AddModifier(LineModifier::RESET);

  auto view_start = static_cast<size_t>(
      max(0, options.buffer->Read(buffer_variables::view_start_column())));
  if ((!target_buffer->Read(buffer_variables::paste_mode()) ||
       target_buffer != options.buffer) &&
      options.line_width != 0 && view_start < options.line_width &&
      options.line_width - view_start < options.width) {
    if (options.line_width > view_start + options.output_receiver->column()) {
      size_t padding =
          options.line_width - view_start - options.output_receiver->column();
      for (auto it : options_.end_of_line_modifiers) {
        options.output_receiver->AddModifier(it);
      }
      options.output_receiver->AddString(wstring(padding, L' '));
      options.output_receiver->AddModifier(LineModifier::RESET);
      CHECK_LE(options.output_receiver->column(), options.width);
    }

    auto all_marks = options.buffer->GetLineMarks();
    auto marks = all_marks->equal_range(options.position.line);

    wchar_t info_char = L'•';
    wstring additional_information;

    if (target_buffer != options.buffer) {
      if (options.output_buffers_shown->insert(target_buffer).second) {
        additional_information = target_buffer->FlagsString();
      }
    } else if (marks.first != marks.second) {
      options.output_receiver->AddModifier(LineModifier::RED);
      info_char = '!';

      // Prefer fresh over expired marks.
      while (next(marks.first) != marks.second &&
             marks.first->second.IsExpired()) {
        ++marks.first;
      }

      const LineMarks::Mark& mark = marks.first->second;
      if (mark.source_line_content != nullptr) {
        additional_information =
            L"(old) " + mark.source_line_content->ToString();
      } else {
        auto source = options.editor_state->buffers()->find(mark.source);
        if (source != options.editor_state->buffers()->end() &&
            source->second->contents()->size() > mark.source_line) {
          options.output_receiver->AddModifier(LineModifier::BOLD);
          additional_information =
              source->second->contents()->at(mark.source_line)->ToString();
        } else {
          additional_information = L"(dead)";
        }
      }
    } else if (modified_) {
      options.output_receiver->AddModifier(LineModifier::GREEN);
      info_char = L'•';
    } else {
      options.output_receiver->AddModifier(LineModifier::DIM);
    }
    if (options.output_receiver->column() <= options.line_width &&
        options.output_receiver->column() < options.width) {
      options.output_receiver->AddCharacter(info_char);
    }
    options.output_receiver->AddModifier(LineModifier::RESET);

    if (additional_information.empty()) {
      auto parse_tree = options.buffer->simplified_parse_tree();
      if (parse_tree != nullptr) {
        additional_information += DrawTree(
            options.position.line, options.buffer->lines_size(), *parse_tree);
      }
      if (options.buffer->Read(buffer_variables::scrollbar()) &&
          options.buffer->lines_size() > options.lines_to_show) {
        additional_information += ComputeScrollBarCharacter(
            options.position.line, options.buffer->lines_size(),
            view_start_line, options.lines_to_show);
      }
      if (options.full_file_parse_tree != nullptr &&
          !options.full_file_parse_tree->children.empty()) {
        additional_information +=
            DrawTree(options.position.line - view_start_line,
                     options.lines_to_show, *options.full_file_parse_tree);
      }
    }

    if (options.output_receiver->column() > options.line_width + 1) {
      VLOG(6) << "Trimming the beginning of additional_information.";
      additional_information = additional_information.substr(
          min(additional_information.size(),
              options.output_receiver->column() - (options.line_width + 1)));
    }
    if (options.width >= options.output_receiver->column()) {
      VLOG(6) << "Trimming the end of additional_information.";
      additional_information = additional_information.substr(
          0, min(additional_information.size(),
                 options.width - options.output_receiver->column()));
    }
    options.output_receiver->AddString(additional_information);
  }

  if (options.output_receiver->column() < options.width) {
    VLOG(6) << "Adding newline characters.";
    options.output_receiver->AddString(L"\n");
  }
}

OutputReceiverOptimizer::~OutputReceiverOptimizer() { Flush(); }

void OutputReceiverOptimizer::AddCharacter(wchar_t character) {
  if (last_modifiers_ != modifiers_) {
    Flush();
  }
  buffer_.push_back(character);
}

void OutputReceiverOptimizer::AddString(const wstring& str) {
  if (last_modifiers_ != modifiers_) {
    Flush();
  }
  buffer_.append(str);
}

void OutputReceiverOptimizer::AddModifier(LineModifier modifier) {
  if (modifier == LineModifier::RESET) {
    modifiers_.clear();
  } else {
    modifiers_.insert(modifier);
  }
}

void OutputReceiverOptimizer::Flush() {
  DCHECK(modifiers_.find(LineModifier::RESET) == modifiers_.end());
  DCHECK(last_modifiers_.find(LineModifier::RESET) == last_modifiers_.end());

  if (!buffer_.empty()) {
    delegate_->AddString(buffer_);
    buffer_.clear();
  }

  if (!std::includes(modifiers_.begin(), modifiers_.end(),
                     last_modifiers_.begin(), last_modifiers_.end())) {
    DVLOG(5) << "last_modifiers_ is not contained in modifiers_.";
    delegate_->AddModifier(LineModifier::RESET);
    last_modifiers_.clear();
  }

  for (auto& modifier : modifiers_) {
    auto inserted = last_modifiers_.insert(modifier).second;
    if (inserted) {
      delegate_->AddModifier(modifier);
    }
  }
  DCHECK(last_modifiers_ == modifiers_);
}

size_t OutputReceiverOptimizer::column() {
  Flush();
  return delegate_->column();
}

}  // namespace editor
}  // namespace afc
