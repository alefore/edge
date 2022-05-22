#ifndef __AFC_EDITOR_INSERT_HISTORY_H__
#define __AFC_EDITOR_INSERT_HISTORY_H__

#include <memory>
#include <optional>
#include <vector>

#include "src/buffer_contents.h"
#include "src/language/safe_types.h"

namespace afc::editor {
class InsertHistory {
 public:
  InsertHistory() = default;

  void Append(const BufferContents& insertion);

  struct SearchOptions {
    std::wstring query;
  };

  // Return the entry from the history that best fits `search_options`. For now,
  // that's just the most recent entry.
  std::optional<language::NonNull<const BufferContents*>> Search(
      SearchOptions search_options);

 private:
  std::vector<language::NonNull<std::unique_ptr<const BufferContents>>>
      history_;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_INSERT_HISTORY_H__
