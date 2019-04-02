#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/cursors.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/tree.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;
using std::vector;

class BufferContents {
 public:
  BufferContents() = default;

  wint_t character_at(const LineColumn& position) const;

  bool empty() const { return lines_.empty(); }

  size_t size() const { return lines_.size(); }

  // Returns a copy of the contents of the tree. Complexity is linear to the
  // number of lines (the lines themselves aren't actually copied).
  std::unique_ptr<BufferContents> copy() const;

  shared_ptr<const Line> at(size_t position) const {
    return lines_.at(position);
  }

  shared_ptr<const Line> back() const {
    CHECK(!empty());
    return lines_.at(size() - 1);
  }

  shared_ptr<const Line> front() const {
    CHECK(!empty());
    return lines_.at(0);
  }

  // Iterates: runs the callback on every line in the buffer, passing as the
  // first argument the line count (starts counting at 0). Stops the iteration
  // if the callback returns false. Returns true iff the callback always
  // returned true.
  // TODO: Use an enum as the return value.
  bool ForEach(const std::function<bool(size_t, const Line&)>& callback) const;

  // Convenience wrappers of the above.
  void ForEach(const std::function<void(const Line&)>& callback) const;
  void ForEach(const std::function<void(wstring)>& callback) const;

  wstring ToString() const;

  template <class C>
  size_t upper_bound(std::shared_ptr<const Line>& key, C compare) const {
    auto it = std::upper_bound(lines_.begin(), lines_.end(), key, compare);
    return distance(lines_.begin(), it);
  }

  size_t CountCharacters() const;

  void insert_line(size_t line_position, shared_ptr<const Line> line);

  // Does not call NotifyUpdateListeners! That should be done by the caller.
  // Avoid calling this in general: prefer calling the other functions (that
  // have more semantic information about what you're doing).
  void set_line(size_t position, shared_ptr<const Line> line) {
    if (position >= size()) {
      return push_back(line);
    }

    CHECK_LE(position, size());
    lines_[position] = line;
  }

  template <class C>
  void sort(size_t first, size_t last, C compare) {
    std::sort(lines_.begin() + first, lines_.begin() + last, compare);
    NotifyUpdateListeners(CursorsTracker::Transformation());
  }

  void insert(size_t position_line, const BufferContents& source,
              const LineModifierSet* modifiers);

  // Delete characters from the given line in range [column, column + amount).
  void DeleteCharactersFromLine(size_t line, size_t column, size_t amount);
  // Delete characters from the given line in range [column, ...).
  void DeleteCharactersFromLine(size_t line, size_t column);

  void SetCharacter(size_t line, size_t column, int c,
                    std::unordered_set<LineModifier, hash<int>> modifiers);

  void InsertCharacter(size_t line, size_t column);
  void AppendToLine(size_t line, const Line& line_to_append);

  void EraseLines(size_t first, size_t last);

  void SplitLine(LineColumn position);

  // Appends the next line to the current line and removes the next line.
  // Essentially, removes the \n at the end of the current line.
  void FoldNextLine(size_t line);

  void push_back(wstring str);
  void push_back(shared_ptr<const Line> line) {
    lines_.push_back(line);
    NotifyUpdateListeners(CursorsTracker::Transformation());
  }

  void AddUpdateListener(
      std::function<void(const CursorsTracker::Transformation&)> listener);

 private:
  void NotifyUpdateListeners(
      const CursorsTracker::Transformation& cursor_adjuster);

  Tree<shared_ptr<const Line>> lines_;
  vector<std::function<void(const CursorsTracker::Transformation&)>>
      update_listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
