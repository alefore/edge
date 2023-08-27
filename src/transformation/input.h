#ifndef __AFC_EDITOR_TRANSFORMATION_INPUT_H__
#define __AFC_EDITOR_TRANSFORMATION_INPUT_H__

#include <optional>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line_column.h"

namespace afc::editor {
class OpenBuffer;
class BufferContents;
class CompositeTransformation;
namespace transformation {
struct Input {
  class Adapter {
   public:
    virtual ~Adapter() = default;
    virtual const BufferContents& contents() const = 0;

    virtual void SetActiveCursors(
        std::vector<language::text::LineColumn> positions) = 0;
    virtual language::text::LineColumn InsertInPosition(
        const BufferContents& contents_to_insert,
        const language::text::LineColumn& input_position,
        const std::optional<LineModifierSet>& modifiers) = 0;
  };

  explicit Input(Adapter& adapter, editor::OpenBuffer& input_buffer);
  Input NewChild(language::text::LineColumn new_position) const;

  // Input parameter.
  enum class Mode {
    // Just preview what this transformation would do. Don't apply any
    // long-lasting effects.
    kPreview,
    // Apply the transformation.
    kFinal,
  };
  // Input parameter.
  Mode mode = Mode::kFinal;

  Adapter& adapter;

  // The buffer that the transformation should modify.
  //
  // TODO(2023-08-23): Remove access to `buffer`; replace it with methods on
  // `adapter`. Remove this.
  editor::OpenBuffer& buffer;

  // If non-null, if the transformation deletes text, it should append it to
  // this buffer (for pasting it later).
  editor::OpenBuffer* delete_buffer = nullptr;

  // Where should the transformation be applied?
  language::text::LineColumn position;
};
}  // namespace transformation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_TRANSFORMATION_INPUT_H__
