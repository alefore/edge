#include "line.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "wstring.h"

namespace afc {
namespace editor {

using std::hash;
using std::unordered_set;
using std::wstring;

Line::Line(wstring x) : Line(Line::Options(NewCopyString(std::move(x)))) {}

Line::Line(const Options& options)
    : environment_(options.environment == nullptr
                       ? std::make_shared<Environment>() : options.environment),
      contents_(options.contents),
      modifiers_(options.modifiers) {
  CHECK(contents_ != nullptr);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

Line::Line(const Line& line) {
  std::unique_lock<std::mutex> lock(line.mutex_);
  environment_ = line.environment_;
  contents_ = line.contents_;
  modifiers_ = line.modifiers_;
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
  contents_ = StringAppend(
      afc::editor::Substring(contents_, 0, position),
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
  contents_ = StringAppend(
      StringAppend(afc::editor::Substring(contents_, 0, position),
                   NewCopyString(L" ")),
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
  shared_ptr<LazyString> str = NewCopyString(wstring(1, c));
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
  output->at(pos) =
      (pos + 1 == output->size() || output->at(pos + 1) == L' ' ||
       output->at(pos + 1) == L'│')
          ? final_char
          : connect_final_char;
}

wstring DrawTree(size_t line, size_t lines_size, const ParseTree& root) {
  vector<const ParseTree*> route_begin;
  if (line > 0) {
    route_begin = MapRoute(root,
        FindRouteToPosition(root,
            LineColumn(line - 1, std::numeric_limits<size_t>::max())));
  }

  vector<const ParseTree*> route_end;
  if (line < lines_size - 1) {
    route_end = MapRoute(root,
        FindRouteToPosition(root,
            LineColumn(line, std::numeric_limits<size_t>::max())));
  }

  wstring output(root.depth + 1, L' ');
  size_t index_begin = 0;
  size_t index_end = 0;
  while (index_begin < route_begin.size() || index_end < route_end.size()) {
    if (index_begin == route_begin.size()) {
      Draw(route_end[index_end]->depth, L'─', L'┐', L'┬', &output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth, L'─', L'┘', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth > route_end[index_end]->depth) {
      Draw(route_begin[index_begin]->depth, L'─', L'┘', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth > route_begin[index_begin]->depth) {
      Draw(route_end[index_end]->depth, L'─', L'┐', L'┬', &output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth] = L'│';
      index_begin ++;
      index_end ++;
      continue;
    }

    Draw(route_end[index_end]->depth, L'─', L'┤', L'┼', &output);
    index_begin ++;
    index_end ++;
  }
  return output;
}
}

void Line::Output(const EditorState* editor_state,
                  const shared_ptr<OpenBuffer>& buffer,
                  size_t line,
                  OutputReceiverInterface* receiver,
                  size_t width,
                  std::unordered_set<OpenBuffer*>* buffers_shown) const {
  std::unique_lock<std::mutex> lock(mutex_);
  VLOG(5) << "Producing output of line: " << ToString();
  size_t output_column = 0;
  size_t input_column = buffer->Read(buffer->variable_view_start_column());
  unordered_set<LineModifier, hash<int>> current_modifiers;
  while (input_column < contents_->size() && output_column < width) {
    wint_t c = contents_->get(input_column);
    CHECK(c != '\n');
    // TODO: Optimize.
    if (input_column >= modifiers_.size()) {
      receiver->AddModifier(LineModifier::RESET);
    } else if (modifiers_[input_column] != current_modifiers) {
      receiver->AddModifier(LineModifier::RESET);
      current_modifiers = modifiers_[input_column];
      for (auto it : current_modifiers) {
        receiver->AddModifier(it);
      }
    }
    switch (c) {
      case '\r':
        break;
      case '\t':
        {
          size_t new_output_column = min(width,
              8 * static_cast<size_t>(
                  1 + floor(static_cast<double>(output_column) / 8.0)));
          assert(new_output_column > output_column);
          assert(new_output_column - output_column <= 8);
          receiver->AddString(wstring(new_output_column - output_column, ' '));
          output_column = new_output_column;
        }
        break;
      default:
        if (iswprint(c)) {
          VLOG(8) << "Print character: " << c;
          receiver->AddCharacter(c);
          output_column++;
        } else {
          VLOG(7) << "Ignoring non-printable character: " << c;
        }
    }
    input_column++;
  }

  CHECK(environment_ != nullptr);
  auto target_buffer_value = environment_->Lookup(L"buffer");
  const auto target_buffer =
      (target_buffer_value != nullptr
       && target_buffer_value->type.type == VMType::OBJECT_TYPE
       && target_buffer_value->type.object_type == L"Buffer"
       && target_buffer_value->user_value != nullptr)
          ? std::static_pointer_cast<OpenBuffer>(
                target_buffer_value->user_value)
          : buffer;
  size_t line_width = target_buffer->Read(OpenBuffer::variable_line_width());

  auto view_start = static_cast<size_t>(
      max(0, buffer->Read(OpenBuffer::variable_view_start_column())));
  if ((!target_buffer->read_bool_variable(OpenBuffer::variable_paste_mode())
       || target_buffer != buffer)
      && line_width != 0
      && view_start < line_width
      && output_column <= line_width - view_start
      && line_width - view_start < width) {
    size_t padding = line_width - view_start - output_column;
    receiver->AddString(wstring(padding, L' '));

    auto all_marks = buffer->GetLineMarks(*editor_state);
    auto marks = all_marks->equal_range(line);

    wchar_t info_char = L'•';
    wstring additional_information;

    if (target_buffer != buffer) {
      if (buffers_shown->insert(target_buffer.get()).second) {
        additional_information = target_buffer->FlagsString();
      }
    } else if (marks.first != marks.second) {
      receiver->AddModifier(LineModifier::RED);
      info_char = '!';

      // Prefer fresh over expired marks.
      while (next(marks.first) != marks.second
             && marks.first->second.IsExpired()) {
        ++marks.first;
      }

      const LineMarks::Mark& mark = marks.first->second;
      if (mark.source_line_content != nullptr) {
        additional_information =
            L"(old) " + mark.source_line_content->ToString();
      } else {
        auto source = editor_state->buffers()->find(mark.source);
        if (source != editor_state->buffers()->end()
            && source->second->contents()->size() > mark.source_line) {
          receiver->AddModifier(LineModifier::BOLD);
          additional_information =
              source->second->contents()->at(mark.source_line)->ToString();
        } else {
          additional_information = L"(dead)";
        }
      }
    } else if (modified_) {
      receiver->AddModifier(LineModifier::GREEN);
      info_char = L'•';
    } else {
      receiver->AddModifier(LineModifier::DIM);
    }
    receiver->AddCharacter(info_char);
    receiver->AddModifier(LineModifier::RESET);
    output_column += padding + 1;
    CHECK_LE(output_column, width);

    auto root = buffer->simplified_parse_tree();
    if (additional_information.empty() && root != nullptr) {
      additional_information = DrawTree(line, buffer->lines_size(), *root);
    }

    additional_information = additional_information.substr(
        0, min(additional_information.size(), width - output_column));
    receiver->AddString(additional_information);
    output_column += additional_information.size();
  }

  if (output_column < width) {
    receiver->AddModifier(LineModifier::RESET);
    VLOG(6) << "Adding newline characters.";
    receiver->AddString(L"\n");
  }
}

OutputReceiverOptimizer::~OutputReceiverOptimizer() {
  Flush();
}

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
    DVLOG(5) << "Last modifiers not contained in new modifiers.";
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

}  // namespace editor
}  // namespace afc
