#ifndef __AFC_EDITOR_BUFFER_TRANSFORMATION_ADAPTER
#define __AFC_EDITOR_BUFFER_TRANSFORMATION_ADAPTER

#include <vector>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line.h"
#include "src/transformation/input.h"

namespace afc::editor {
class OpenBuffer;
class TransformationInputAdapterImpl : public transformation::Input::Adapter {
  OpenBuffer& buffer_;

 public:
  TransformationInputAdapterImpl(OpenBuffer& buffer) : buffer_(buffer) {}

  const language::text::LineSequence contents() const override;

  void SetActiveCursors(
      std::vector<language::text::LineColumn> positions) override;

  language::text::LineColumn InsertInPosition(
      const language::text::LineSequence& contents_to_insert,
      const language::text::LineColumn& input_position,
      const std::optional<infrastructure::screen::LineModifierSet>& modifiers)
      override;

  void AddError(language::Error error) override;

  void AddFragment(language::text::LineSequence fragment) override;
  futures::Value<language::text::LineSequence> FindFragment() override;
};
}  // namespace afc::editor

#endif
