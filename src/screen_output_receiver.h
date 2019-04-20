#ifndef __AFC_EDITOR_SCREEN_OUTPUT_RECEIVER_H__
#define __AFC_EDITOR_SCREEN_OUTPUT_RECEIVER_H__

#include <string>

#include "src/output_receiver.h"
#include "src/screen.h"

namespace afc {
namespace editor {
std::unique_ptr<OutputReceiver> NewScreenOutputReceiver(Screen* screen);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SCREEN_OUTPUT_RECEIVER_H__
