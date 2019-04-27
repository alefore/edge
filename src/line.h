#ifndef __AFC_EDITOR_LINE_H__
#define __AFC_EDITOR_LINE_H__

#include <glog/logging.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/lazy_string.h"
#include "src/output_receiver.h"
#include "src/parse_tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class EditorMode;
class EditorState;
class LazyString;
class OpenBuffer;

using std::hash;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::unordered_set;
using std::vector;
using std::wstring;

// This class is thread-safe.
class Line {
 public:
  struct Options {
    Options() : contents(EmptyString()) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(std::move(input_contents)), modifiers(contents->size()) {}

    shared_ptr<LazyString> contents;
    vector<LineModifierSet> modifiers;
    LineModifierSet end_of_line_modifiers;
    std::shared_ptr<vm::Environment> environment = nullptr;
  };

  Line() : Line(Options()) {}
  explicit Line(const Options& options);
  explicit Line(wstring text);
  Line(const Line& line);

  shared_ptr<LazyString> contents() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return contents_;
  }
  size_t size() const {
    CHECK(contents() != nullptr);
    return contents()->size();
  }
  bool empty() const {
    CHECK(contents() != nullptr);
    return size() == 0;
  }
  wint_t get(size_t column) const {
    CHECK_LT(column, contents()->size());
    return contents()->get(column);
  }
  shared_ptr<LazyString> Substring(size_t pos, size_t length) const;
  // Returns the substring from pos to the end of the string.
  shared_ptr<LazyString> Substring(size_t pos) const;
  wstring ToString() const { return contents()->ToString(); }
  // Delete characters in [position, position + amount).
  void DeleteCharacters(size_t position, size_t amount);
  // Delete characters from position until the end.
  void DeleteCharacters(size_t position);
  void InsertCharacterAtPosition(size_t position);

  // Sets the character at the position given.
  //
  // `position` may be greater than size(), in which case the character will
  // just get appended (extending the line by exactly one character).
  void SetCharacter(size_t position, int c, const LineModifierSet& modifiers);

  void SetAllModifiers(const LineModifierSet& modifiers);
  const vector<LineModifierSet> modifiers() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return modifiers_;
  }
  vector<LineModifierSet>& modifiers() {
    std::unique_lock<std::mutex> lock(mutex_);
    return modifiers_;
  }
  const LineModifierSet& end_of_line_modifiers() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return options_.end_of_line_modifiers;
  }

  bool modified() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return modified_;
  }
  void set_modified(bool modified) {
    std::unique_lock<std::mutex> lock(mutex_);
    modified_ = modified;
  }

  void Append(const Line& line);

  std::shared_ptr<vm::Environment> environment() const;

  bool filtered() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return filtered_;
  }
  bool filter_version() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return filter_version_;
  }
  void set_filtered(bool filtered, size_t filter_version) {
    std::unique_lock<std::mutex> lock(mutex_);
    filtered_ = filtered;
    filter_version_ = filter_version;
  }

  struct OutputOptions {
    OutputReceiver* output_receiver = nullptr;
    size_t initial_column;
    size_t width = 0;
  };
  void Output(const OutputOptions& options) const;

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<vm::Environment> environment_;
  // TODO: Remove contents_ and modifiers_ and just use options_ instead.
  shared_ptr<LazyString> contents_;
  vector<LineModifierSet> modifiers_;
  Options options_;
  bool modified_ = false;
  bool filtered_ = true;
  size_t filter_version_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_H__
