#ifndef __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__

#include "src/buffer_contents_view_layout.h"
#include "src/columns_vector.h"
#include "src/line_with_cursor.h"

namespace afc::editor {
class OpenBuffer;

// It is OK for all referenced objects to be deleted after BufferMetadataOutput
// has returned.
struct BufferMetadataOutputOptions {
  const OpenBuffer& buffer;
  const std::vector<BufferContentsViewLayout::Line>& screen_lines;
  const ParseTree& zoomed_out_tree;
};

// OutputProducer that prints the metadata that is usually shown right after the
// contents of the buffer (at the right side).
//
// Generates one element for each value in screen_lines.
ColumnsVector::Column BufferMetadataOutput(BufferMetadataOutputOptions options);

}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
