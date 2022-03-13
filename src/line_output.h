// Helper functions for Line class.

#ifndef __AFC_EDITOR_LINE_OUTPUT_H__
#define __AFC_EDITOR_LINE_OUTPUT_H__

#include "src/line.h"

namespace afc::editor {
// If the line is printed to the screen starting at its position `begin` and the
// screen can hold up to `screen_positions` characters, how many characters
// should be consumed from the input?
//
// Takes into account double-width characters.
ColumnNumberDelta LineOutputLength(const Line& line, ColumnNumber begin,
                                   ColumnNumberDelta screen_positions);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_OUTPUT_H__
