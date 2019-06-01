#ifndef __AFC_EDITOR_LINE_H__
#define __AFC_EDITOR_LINE_H__

#include <glog/logging.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"
#include "src/output_producer.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class EditorMode;
class EditorState;
class LazyString;
class OpenBuffer;
class LineWithCursor;

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
  // TODO: Turn this into a class.
  struct Options {
    Options() : contents(EmptyString()) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(std::move(input_contents)) {}
    Options(Line line);

    ColumnNumber EndColumn() const;

    void AppendCharacter(wchar_t c, LineModifierSet modifier);
    void AppendString(std::shared_ptr<LazyString> suffix);
    void AppendString(std::shared_ptr<LazyString> suffix,
                      LineModifierSet modifier);
    void AppendString(std::wstring contents, LineModifierSet modifier);
    void Append(Line line);
    Options& DeleteCharacters(ColumnNumber position, ColumnNumberDelta amount);

    // Delete characters from column (included) until the end.
    Options& DeleteSuffix(ColumnNumber column);

    std::shared_ptr<LazyString> contents;

    // Columns without an entry here reuse the last present value. If no
    // previous value, assume LineModifierSet(). There's no need to include
    // RESET: it is assumed implicitly. In other words, modifiers don't carry
    // over past an entry.
    std::map<ColumnNumber, LineModifierSet> modifiers;

    LineModifierSet end_of_line_modifiers;
    std::shared_ptr<vm::Environment> environment;
    size_t hash = 0;

   private:
    void ValidateInvariants();
  };

  static std::shared_ptr<Line> New(Options options);
  Line() : Line(Options()) {}
  explicit Line(Options options);
  explicit Line(wstring text);
  Line(const Line& line);

  shared_ptr<LazyString> contents() const;
  ColumnNumber EndColumn() const;
  bool empty() const;

  wint_t get(ColumnNumber column) const;
  shared_ptr<LazyString> Substring(ColumnNumber column,
                                   ColumnNumberDelta length) const;

  // Returns the substring from pos to the end of the string.
  shared_ptr<LazyString> Substring(ColumnNumber column) const;

  wstring ToString() const { return contents()->ToString(); }
  // Delete characters in [position, position + amount).

  void InsertCharacterAtPosition(ColumnNumber position);

  // Sets the character at the position given.
  //
  // `position` may be greater than size(), in which case the character will
  // just get appended (extending the line by exactly one character).
  void SetCharacter(ColumnNumber position, int c,
                    const LineModifierSet& modifiers);

  void SetAllModifiers(const LineModifierSet& modifiers);
  const std::map<ColumnNumber, LineModifierSet>& modifiers() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return options_.modifiers;
  }
  std::map<ColumnNumber, LineModifierSet>& modifiers() {
    std::unique_lock<std::mutex> lock(mutex_);
    return options_.modifiers;
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
    ColumnNumber initial_column;
    ColumnNumberDelta width;
    std::optional<ColumnNumber> active_cursor_column;
    std::set<ColumnNumber> inactive_cursor_columns;
    LineModifierSet modifiers_inactive_cursors;
  };
  OutputProducer::LineWithCursor Output(const OutputOptions& options) const;

  size_t GetHash() const;

 private:
  void ValidateInvariants() const;
  ColumnNumber EndColumnWithLock() const;
  wint_t GetWithLock(ColumnNumber column) const;

  mutable std::mutex mutex_;
  std::shared_ptr<vm::Environment> environment_;
  Options options_;
  bool modified_ = false;
  bool filtered_ = true;
  size_t filter_version_ = 0;

  mutable std::optional<size_t> hash_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_H__
