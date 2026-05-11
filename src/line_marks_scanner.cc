#include "src/buffer_name.h"
#include "src/editor.h"
#include "src/file_predictor.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/view.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/line_marks.h"
#include "src/open_file_position.h"

using afc::infrastructure::Path;
using afc::language::EmptyValue;
using afc::language::ValueOrError;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::view::SkipErrors;

namespace afc::editor {
void ScanLineMarks(EditorState& editor, BufferName buffer_name,
                   LineSequence contents, LineNumberDelta start_new_section,
                   LineNumberDelta lines_added) {
  TRACK_OPERATION(OpenBuffer_StartNewLine_ScanForMarks);
  std::function<futures::Value<PredictorOutput>(PredictorInput)>
      file_predictor = GetFilePredictor(FilePredictorOptions{
          .match_type = FilePredictorMatchType::Exact,
          .open_file_position_suffix_mode =
              open_file_position::SuffixMode::Allow,
          .output_format = FilePredictorOutputFormat::SearchPathAndInput});
  for (LineNumberDelta i; i < lines_added; ++i) {
    LineNumber source_line = LineNumber{} + start_new_section + i;
    Line source_line_content = contents.at(source_line).value;
    file_predictor(PredictorInput{.editor = editor,
                                  .input = source_line_content.contents(),
                                  .input_column = {},
                                  .source_buffers = {}})
        .Transform([buffer_name, &editor, source_line](PredictorOutput output) {
          std::ranges::for_each(
              output.contents.read().lines() |
                  std::views::transform(
                      [&buffer_name, &editor, &source_line](
                          const Line& line) -> ValueOrError<LineMarks::Mark> {
                        DECLARE_OR_RETURN(
                            Path target_buffer,
                            Path::New(ToLazyString(line.contents())));
                        open_file_position::Spec spec =
                            open_file_position::SpecFromLineMetadata(
                                line.metadata().get());
                        return LineMarks::Mark{
                            .source_buffer = buffer_name,
                            .source_line = source_line,
                            .source_line_content = line,
                            .target_buffer = BufferFileId(target_buffer),
                            .target_line_column =
                                std::holds_alternative<LineColumn>(spec)
                                    ? std::get<LineColumn>(spec)
                                    : LineColumn{}};
                      }) |
                  SkipErrors,
              [&editor](LineMarks::Mark mark) {
                LOG(INFO) << "Found a mark: " << mark;
                editor.line_marks().AddMark(std::move(mark));
              });
          return EmptyValue{};
        });
  }
}
}  // namespace afc::editor