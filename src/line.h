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

#include "src/futures/futures.h"
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
  struct MetadataEntry {
    std::shared_ptr<LazyString> initial_value;
    futures::ListenableValue<std::shared_ptr<LazyString>> value;
  };

  class Options {
   public:
    Options() : contents(EmptyString()) {}
    Options(shared_ptr<LazyString> input_contents)
        : contents(std::move(input_contents)) {}
    Options(Line line);

    ColumnNumber EndColumn() const;

    // Sets the character at the position given.
    //
    // `column` may be greater than size(), in which case the character will
    // just get appended (extending the line by exactly one character).
    void SetCharacter(ColumnNumber column, int c,
                      const LineModifierSet& modifiers);

    void InsertCharacterAtPosition(ColumnNumber position);
    void AppendCharacter(wchar_t c, LineModifierSet modifier);
    void AppendString(std::shared_ptr<LazyString> suffix);
    void AppendString(std::shared_ptr<LazyString> suffix,
                      std::optional<LineModifierSet> modifier);
    void AppendString(std::wstring contents,
                      std::optional<LineModifierSet> modifier);
    void Append(Line line);

    Options& SetMetadata(std::optional<MetadataEntry> metadata);

    // Delete characters in [position, position + amount).
    Options& DeleteCharacters(ColumnNumber position, ColumnNumberDelta amount);

    // Delete characters from column (included) until the end.
    Options& DeleteSuffix(ColumnNumber column);

    // TODO: Make these fields private.
    std::shared_ptr<LazyString> contents;

    // Columns without an entry here reuse the last present value. If no
    // previous value, assume LineModifierSet(). There's no need to include
    // RESET: it is assumed implicitly. In other words, modifiers don't carry
    // over past an entry.
    std::map<ColumnNumber, LineModifierSet> modifiers;

    // The semantics of this is that any characters at the end of the line
    // (i.e., the space that represents the end of the line) should be rendered
    // using these modifiers.
    //
    // If two lines are concatenated, the end of line modifiers of the first
    // line is entirely ignored; it doesn't affect the first characters from the
    // second line.
    LineModifierSet end_of_line_modifiers;

   private:
    friend Line;

    std::optional<MetadataEntry> metadata = std::nullopt;
    std::shared_ptr<vm::Environment> environment = nullptr;
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

  std::shared_ptr<LazyString> metadata() const;

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
    // Total number of screen characters to consume. If the input has wide
    // characters, they have to be taken into account (in other words, the
    // number of characters consumed from the input may be smaller than the
    // width).
    ColumnNumberDelta width;
    // Maximum number of characters in the input to consume. Even if more
    // characters would fit in the output (per `width`), can stop outputting
    // when this limit is reached.
    ColumnNumberDelta input_width;
    std::optional<ColumnNumber> active_cursor_column = std::nullopt;
    std::set<ColumnNumber> inactive_cursor_columns = {};
    LineModifierSet modifiers_main_cursor = {};
    LineModifierSet modifiers_inactive_cursors = {};
  };
  OutputProducer::LineWithCursor Output(const OutputOptions& options) const;

  size_t GetHash() const;

 private:
  void ValidateInvariants() const;
  ColumnNumber EndColumnWithLock() const;
  wint_t GetWithLock(ColumnNumber column) const;

  friend class Options;

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
