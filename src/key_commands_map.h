#ifndef __AFC_EDITOR_KEY_COMMANDS_MAP_H__
#define __AFC_EDITOR_KEY_COMMANDS_MAP_H__

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace afc::editor::operation {
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
    std::wstring description = L"";
    bool active = true;
    std::function<void(wchar_t)> handler;
  };

 private:
  std::unordered_map<wchar_t, KeyCommand> table_;
  std::set<wchar_t> fallback_exclusion_;
  std::function<void(wchar_t)> fallback_ = nullptr;
  std::function<void()> on_handle_ = nullptr;

 public:
  KeyCommandsMap() = default;
  KeyCommandsMap(const KeyCommandsMap&) = delete;
  KeyCommandsMap(KeyCommandsMap&&) = default;

  KeyCommandsMap& Insert(wchar_t c, KeyCommand command) {
    if (command.active) table_.insert({c, std::move(command)});
    return *this;
  }

  KeyCommandsMap& Insert(std::set<wchar_t> chars, KeyCommand command) {
    if (command.active)
      for (wchar_t c : chars) table_.insert({c, command});
    return *this;
  }

  template <typename Value, typename Callable>
  KeyCommandsMap& Insert(const std::unordered_map<wchar_t, Value>& values,
                         Category category, Callable callback) {
    for (const auto& entry : values)
      Insert(entry.first,
             {.category = category, .handler = [callback, entry](wchar_t) {
                callback(entry.second);
              }});
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

  std::map<wchar_t, KeyCommandsMap::Category> GetKeys() {
    std::map<wchar_t, KeyCommandsMap::Category> output;
    for (const KeyCommandsMap& entry : sequence_) {
      entry.ExtractKeys(output);
      if (entry.HasFallback()) break;
    }
    return output;
  }
};
}  // namespace afc::editor::operation

#endif