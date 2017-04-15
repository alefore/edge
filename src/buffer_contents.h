#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/line.h"
#include "src/tree.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;
using std::vector;

class BufferContents {
 public:
  BufferContents() = default;

  bool empty() const { return lines_.empty(); }

  size_t size() const { return lines_.size(); }

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
  bool ForEach(const std::function<bool(size_t, const Line&)>& callback)
      const;

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

  void insert_line(size_t line_position, shared_ptr<const Line> line) {
    lines_.insert(lines_.begin() + line_position, line);
    NotifyUpdateListeners();
  }

  void set_line(size_t position, shared_ptr<const Line> line) {
    CHECK_LE(position, size());
    lines_[position] = line;
    NotifyUpdateListeners();
  }

  template <class C>
  void sort(size_t first, size_t last, C compare) {
    std::sort(lines_.begin() + first, lines_.begin() + last, compare);
    NotifyUpdateListeners();
  }

  void insert(size_t position, const BufferContents& source,
              size_t source_first_line, size_t source_last_line);

  void DeleteCharactersFromLine(size_t line, size_t column, size_t stop_column);

  void SetCharacter(size_t line, size_t column, int c,
      std::unordered_set<Line::Modifier, hash<int>> modifiers);

  void InsertCharacter(size_t line, size_t column);

  void EraseLines(size_t first, size_t last) {
    CHECK_LE(first, last);
    CHECK_LE(last, size());
    if (first == last) {
      return;  // Optimization to avoid notifying listeners.
    }
    lines_.erase(lines_.begin() + first, lines_.begin() + last);
    NotifyUpdateListeners();
  }

  void push_back(shared_ptr<const Line> line) {
    lines_.push_back(line);
    NotifyUpdateListeners();
  }

  void AddUpdateListener(std::function<void()> listener);

 private:
  void NotifyUpdateListeners();

  Tree<shared_ptr<const Line>> lines_;
  vector<std::function<void()>> update_listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
