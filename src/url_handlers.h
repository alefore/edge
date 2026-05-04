#ifndef __AFC_EDITOR_SRC_URL_HANDLERS_H__
#define __AFC_EDITOR_SRC_URL_HANDLERS_H__

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/url.h"

namespace afc::editor {
class EditorState;
class OpenBuffer;

enum class RemoteURLBehavior { Ignore, LaunchBrowser };
enum class VmURLBehavior { Ignore, Execute };

futures::ValueOrError<std::optional<language::gc::Root<OpenBuffer>>> HandleURL(
    EditorState& editor, ExecutionContext& execution_context,
    RemoteURLBehavior remote_url_behavior, VmURLBehavior vm_url_behavior,
    const URL& url);
}  // namespace afc::editor
#endif
