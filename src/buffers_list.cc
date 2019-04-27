#include "buffers_list.h"

#include <algorithm>
#include <ctgmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/dirname.h"

namespace afc {
namespace editor {

BuffersListProducer::BuffersListProducer(
    std::vector<std::vector<Entry>> buffers, size_t active_index)
    : buffers_(buffers),
      active_index_(active_index),
      max_index_([&]() {
        size_t output = buffers_[0][0].index;
        for (auto& line : buffers_) {
          for (auto& buffer : line) {
            output = std::max(buffer.index, output);
          }
        }
        return output;
      }()),
      prefix_width_(std::to_wstring(max_index_ + 1).size() + 2) {}

void BuffersListProducer::WriteLine(Options options) {
  size_t columns_per_buffer =  // Excluding prefixes and separators.
      (options.receiver->width() -
       std::min(options.receiver->width(),
                (prefix_width_ * buffers_[0].size()))) /
      buffers_[0].size();
  const auto& row = buffers_[current_line_++];
  for (size_t i = 0; i < row.size(); i++) {
    auto buffer = row[i].buffer;
    options.receiver->AddModifier(LineModifier::RESET);
    auto name =
        buffer == nullptr ? L"(dead)" : buffer->Read(buffer_variables::name);
    auto number_prefix = std::to_wstring(row[i].index + 1);
    size_t start = i * (columns_per_buffer + prefix_width_) +
                   (prefix_width_ - number_prefix.size() - 2);
    if (options.receiver->column() < start) {
      options.receiver->AddString(
          wstring(start - options.receiver->column(), L' '));
    }
    options.receiver->AddModifier(LineModifier::CYAN);
    if (row[i].index == active_index_) {
      options.receiver->AddModifier(LineModifier::BOLD);
    }
    options.receiver->AddString(number_prefix);
    options.receiver->AddModifier(LineModifier::RESET);

    std::list<std::wstring> output_components;
    std::list<std::wstring> components;
    if (buffer != nullptr && buffer->Read(buffer_variables::path) == name &&
        DirectorySplit(name, &components) && !components.empty()) {
      name.clear();
      output_components.push_front(components.back());
      if (output_components.front().size() > columns_per_buffer) {
        output_components.front() = output_components.front().substr(
            output_components.front().size() - columns_per_buffer);
      } else {
        size_t consumed = output_components.front().size();
        components.pop_back();

        static const size_t kSizeOfSlash = 1;
        while (!components.empty()) {
          if (columns_per_buffer >
              components.size() * 2 + components.back().size() + consumed) {
            output_components.push_front(components.back());
            consumed += components.back().size() + kSizeOfSlash;
          } else if (columns_per_buffer > 1 + kSizeOfSlash + consumed) {
            output_components.push_front(std::wstring(1, components.back()[0]));
            consumed += 1 + kSizeOfSlash;
          } else {
            break;
          }
          components.pop_back();
        }
      }
    }

    options.receiver->AddModifier(LineModifier::DIM);
    if (!name.empty()) {
      if (name.size() > columns_per_buffer) {
        name = name.substr(name.size() - columns_per_buffer);
        options.receiver->AddString(L"…");
      } else {
        options.receiver->AddString(L":");
      }
      options.receiver->AddModifier(LineModifier::RESET);
      options.receiver->AddString(name);
      continue;
    }

    if (components.empty()) {
      options.receiver->AddString(L":");
    } else {
      options.receiver->AddString(L"…");
    }
    options.receiver->AddModifier(LineModifier::RESET);

    auto last = output_components.end();
    --last;
    for (auto it = output_components.begin(); it != output_components.end();
         ++it) {
      if (it != output_components.begin()) {
        options.receiver->AddModifier(LineModifier::DIM);
        options.receiver->AddCharacter(L'/');
        options.receiver->AddModifier(LineModifier::RESET);
      }
      if (it == last) {
        options.receiver->AddModifier(LineModifier::BOLD);
      }
      options.receiver->AddString(*it);
    }
    options.receiver->AddModifier(LineModifier::RESET);
  }
}

}  // namespace editor
}  // namespace afc
