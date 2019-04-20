#ifndef __AFC_EDITOR_OUTPUT_RECEIVER_H__
#define __AFC_EDITOR_OUTPUT_RECEIVER_H__

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

class OutputReceiver {
 public:
  virtual ~OutputReceiver() = default;

  virtual void AddCharacter(wchar_t character) = 0;
  virtual void AddString(const wstring& str) = 0;
  virtual void AddModifier(LineModifier modifier) = 0;
  virtual void SetTabsStart(size_t columns) = 0;
  virtual size_t column() = 0;
  virtual size_t width() = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_OUTPUT_RECEIVER_H__
