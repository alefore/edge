#ifndef __AFC_EDITOR_KEY_COMMANDS_MAP_H__
#define __AFC_EDITOR_KEY_COMMANDS_MAP_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor::operation {
GHOST_TYPE(Description, std::wstring);

// Contains a table of commands. Each command is an association of a chatacter
// to a "handler" that should be executed when the command is pressed.
// Additionally, an optional "fallback" function is kept, that should be
// executed when a command without a handler is pressed.
//
// For each command, maintains a bit of metadata: a category and a description.
// This is used to print help messages about the available commands.
class KeyCommandsMap {
 public:
  enum class Category {
    kStringControl,
    kRepetitions,
    kDirection,
    kStructure,
    kNewCommand,
    kTop,
  };

  struct KeyCommand {
    Category category;
    Description description;
    bool active = true;
    std::function<void(wchar_t)> handler;
  };

 private:
  std::unordered_map<wchar_t, KeyCommand> table_;
  // The fallback function will be executed if a command is received that
  // doesn't have an entry in either `table_` or `fallback_exclusion_`. This
  // allows us to exclude some characters from the `fallback_` function.
  std::set<wchar_t> fallback_exclusion_;

  std::function<void(wchar_t)> fallback_ = nullptr;

  // Optional function to execute whenever a command's handler or the fallback
  // function is executed.
  std::function<void()> on_handle_ = nullptr;

 public:
  KeyCommandsMap() = default;
  KeyCommandsMap(const KeyCommandsMap&) = delete;
  KeyCommandsMap(KeyCommandsMap&&) = default;

  static language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
  ToString(Category category);

  KeyCommandsMap& Insert(wchar_t c, KeyCommand command) {
    if (command.active) table_.insert({c, std::move(command)});
    return *this;
  }

  KeyCommandsMap& Erase(wchar_t c) {
    table_.erase(c);
    return *this;
  }

  KeyCommandsMap& SetFallback(std::set<wchar_t> exclude,
                              std::function<void(wchar_t)> callback) {
    CHECK(fallback_ == nullptr);
    CHECK(callback != nullptr);
    fallback_exclusion_ = std::move(exclude);
    fallback_ = std::move(callback);
    return *this;
  }

  KeyCommandsMap& OnHandle(std::function<void()> handler) {
    CHECK(on_handle_ == nullptr);
    on_handle_ = handler;
    return *this;
  }

  std::function<void(wchar_t)> FindCallbackOrNull(wchar_t c) const {
    if (auto it = table_.find(c); it != table_.end()) return it->second.handler;
    if (HasFallback() &&
        fallback_exclusion_.find(c) == fallback_exclusion_.end())
      return fallback_;
    return nullptr;
  }

  bool HasFallback() const { return fallback_ != nullptr; }

  bool Execute(wchar_t c) const {
    if (auto callback = FindCallbackOrNull(c); callback != nullptr) {
      callback(c);
      if (on_handle_ != nullptr) on_handle_();
      return true;
    }
    return false;
  }

  void ExtractKeys(std::map<wchar_t, Category>& output) const {
    for (auto& entry : table_)
      output.insert({entry.first, entry.second.category});
  }

  // `consumed` is an input-output parameter containing the list of characters
  // visited. Entries for previously visited characters will be ignored.
  void ExtractDescriptions(
      std::set<wchar_t>& consumed,
      std::map<Category, std::map<wchar_t, Description>>& output) const;
};

class KeyCommandsMapSequence {
  std::vector<KeyCommandsMap> sequence_;

 public:
  bool Execute(wchar_t c) const {
    for (const auto& cmap : sequence_) {
      if (cmap.Execute(c)) return true;
    }
    return false;
  }

  KeyCommandsMapSequence& PushBack(KeyCommandsMap cmap) {
    sequence_.push_back(std::move(cmap));
    return *this;
  }

  KeyCommandsMap& PushNew() {
    PushBack(KeyCommandsMap());
    return sequence_.back();
  }

  std::map<wchar_t, KeyCommandsMap::Category> GetKeys() const;

  language::text::Line SummaryLine() const;
  language::text::LineSequence Help() const;
};
}  // namespace afc::editor::operation

#endif