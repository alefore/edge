#include "line.h"

#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

Line::Line() {};
Line::Line(const shared_ptr<LazyString>& contents_input)
    : contents_(contents_input),
      modified_(false),
      filtered_(true),
      filter_version_(0) {}

shared_ptr<LazyString> Line::Substring(size_t pos, size_t length) {
  return afc::editor::Substring(contents_, pos, length);
}

shared_ptr<LazyString> Line::Substring(size_t pos) {
  return afc::editor::Substring(contents_, pos);
}

void Line::set_activate(unique_ptr<EditorMode> activate) {
  activate_ = std::move(activate);
}

}  // namespace editor
}  // namespace afc
