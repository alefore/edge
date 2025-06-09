#include "src/flashcard.h"

#include <vector>

#include "src/buffer.h"
#include "src/buffer_vm.h"
#include "src/concurrent/protected.h"
#include "src/file_link_mode.h"
#include "src/file_tags.h"
#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/lazy_value.h"
#include "src/language/observers.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/math/numbers.h"
#include "src/vm/container.h"
#include "src/vm/types.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::infrastructure::AbsolutePath;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::LazyValue;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ObservableValue;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::math::numbers::Number;

namespace afc::editor {
namespace {
// TODO(trivial, 2025-06-08): Move to //src/language/lazy_string.
template <typename StringType>
uint64_t fnv1a(const StringType& text) {
  constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
  constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
  uint64_t hash = FNV_OFFSET_BASIS;
  ForEachColumn(text, [&hash](ColumnNumber, wchar_t c) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&c);
    for (std::size_t j = 0; j < sizeof(wchar_t); ++j) {
      hash ^= static_cast<std::uint64_t>(bytes[j]);
      hash *= FNV_PRIME;
    }
  });
  return hash;
}

ValueOrError<Path> BuildReviewLogPath(Path buffer, SingleLine answer) {
  DECLARE_OR_RETURN(Path buffer_dirname, buffer.Dirname());
  DECLARE_OR_RETURN(PathComponent buffer_basename, buffer.Basename());
  DECLARE_OR_RETURN(PathComponent buffer_basename_without_extension,
                    buffer_basename.remove_extension());
  DECLARE_OR_RETURN(
      PathComponent path_from_hash,
      PathComponent::New(ToLazyString(NonEmptySingleLine{fnv1a(answer)})));
  return Path::Join(buffer_dirname,
                    Path::Join(PathComponent::FromString(L".reviews"),
                               Path::Join(buffer_basename_without_extension,
                                          path_from_hash)));
}
}  // namespace

class FlashcardReviewLog {
  gc::Ptr<OpenBuffer> review_buffer_;
  FileTags file_tags_;

 public:
  static futures::ValueOrError<gc::Root<FlashcardReviewLog>> New(
      EditorState& editor, Path review_log_path, SingleLine answer) {
    return OpenOrCreateFile(
               OpenFileOptions{
                   .editor_state = editor,
                   .path = review_log_path,
                   .insertion_type = BuffersList::AddBufferType::kIgnore,
                   .use_search_paths = false})
        .Transform([answer](gc::Root<OpenBuffer> buffer) {
          buffer->Set(buffer_variables::save_on_close, true);
          return buffer->WaitForEndOfFile().Transform(
              [buffer, answer](
                  EmptyValue) -> ValueOrError<gc::Root<FlashcardReviewLog>> {
                DECLARE_OR_RETURN(
                    FileTags file_tags,
                    std::visit(
                        overload{
                            [](FileTags file_tags) -> ValueOrError<FileTags> {
                              return file_tags;
                            },
                            [buffer,
                             answer](Error error) -> ValueOrError<FileTags> {
                              if (buffer->contents().snapshot() ==
                                  LineSequence{}) {
                                buffer->InsertInPosition(
                                    DefaultReviewLogBufferContents(answer),
                                    LineColumn{}, std::nullopt);
                                return FileTags::New(buffer.ptr());
                              } else {
                                Error augmented_error = AugmentError(
                                    buffer->Read(buffer_variables::path) +
                                        LazyString{L": Unable to parse "
                                                   L"non-empty file"},
                                    error);
                                LOG(INFO) << augmented_error;
                                return augmented_error;
                              }
                            }},
                        FileTags::New(buffer.ptr())));
                return buffer->editor().gc_pool().NewRoot(
                    MakeNonNullUnique<FlashcardReviewLog>(
                        buffer.ptr(), std::move(file_tags)));
              });
        });
  }

  FlashcardReviewLog(gc::Ptr<OpenBuffer> review_buffer, FileTags file_tags)
      : review_buffer_(std::move(review_buffer)),
        file_tags_(std::move(file_tags)) {}

  gc::Ptr<OpenBuffer> buffer() { return review_buffer_; }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    return {review_buffer_.object_metadata()};
  }

 private:
  static LineSequence DefaultReviewLogBufferContents(SingleLine answer) {
    MutableLineSequence output;
    output.AppendToLine(LineNumber{},
                        Line{SINGLE_LINE_CONSTANT(L"# Flashcard review log")});
    output.push_back(L"");
    output.push_back(L"## Tags");
    output.push_back(L"");
    output.push_back(Line{SINGLE_LINE_CONSTANT(L"Answer: ") + answer});
    output.push_back(L"");
    return output.snapshot();
  }
};

class Flashcard {
  // TODO(2025-06-09, trivial): MAke this `const`?
  gc::Ptr<OpenBuffer> buffer_;

  // LazyValue<> objects only retain a gc::Ptr<> (rather than gc::Root<>) to the
  // objects they create. However, they must ensure that the pointers are
  // exposes through `Expand` to the pool before releasing the roots. We use
  // object_metadata_ for that purpose.
  NonNull<std::shared_ptr<
      Protected<std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>>>>
      object_metadata_;

  SingleLine answer_;
  SingleLine hint_;

  // This should ideally be a futures::Value; however, we can't properly
  // propagate the Root -> Ptr value without a very brief period of time where
  // the GC system may think that the object is unreachable (after the root has
  // been destroyed but before the ptr has been installed here).
  NonNull<std::shared_ptr<
      ObservableValue<ValueOrError<gc::Ptr<FlashcardReviewLog>>>>>
      review_log_;

  LazyValue<futures::ListenableValue<gc::Ptr<OpenBuffer>>> card_front_buffer_;

 public:
  static ValueOrError<Flashcard> New(gc::Ptr<OpenBuffer> buffer,
                                     LazyString tag_value) {
    DECLARE_OR_RETURN(SingleLine tag_value_line, SingleLine::New(tag_value));
    std::vector<Token> tokens = TokenizeBySpaces(tag_value_line);
    if (tokens.size() != 2)
      return Error{
          ToSingleLine(buffer->name()) +
          LazyString{L": Invalid flashcard data (expected 2 tokens, found "} +
          NonEmptySingleLine{tokens.size()} + LazyString{L")."}};

    SingleLine answer = tokens[0].value.read();
    DECLARE_OR_RETURN(Path buffer_path,
                      AbsolutePath::New(buffer->Read(buffer_variables::path)));
    DECLARE_OR_RETURN(Path review_log_path,
                      BuildReviewLogPath(buffer_path, answer));

    return Flashcard(
        std::move(buffer), answer, tokens[1].value.read(),
        FlashcardReviewLog::New(buffer->editor(), review_log_path, answer));
  }

  Flashcard(
      gc::Ptr<OpenBuffer> buffer, SingleLine answer, SingleLine hint,
      futures::ValueOrError<gc::Root<FlashcardReviewLog>> future_review_log)
      : buffer_(std::move(buffer)),
        answer_(std::move(answer).read()),
        hint_(std::move(hint).read()),
        card_front_buffer_([&editor = buffer_->editor(),
                            protected_object_metadata = object_metadata_,
                            contents = buffer_->contents().snapshot()] {
          LOG(INFO) << "Starting computation of card front.";
          return futures::ListenableValue(OpenAnonymousBuffer(editor).Transform(
              [&editor, protected_object_metadata,
               contents](gc::Root<OpenBuffer> output_buffer) {
                output_buffer->InsertInPosition(
                    LineSequence::WithLine(
                        Line{SINGLE_LINE_CONSTANT(L"## Flashcard")}) +
                        contents,
                    LineColumn{}, std::nullopt);
                protected_object_metadata->lock(
                    [&output_buffer](
                        std::vector<
                            NonNull<std::shared_ptr<gc::ObjectMetadata>>>&
                            object_metadata) {
                      object_metadata.push_back(
                          output_buffer.ptr().object_metadata());
                    });
                editor.AddBuffer(output_buffer,
                                 BuffersList::AddBufferType::kVisit);
                return output_buffer.ptr();
              }));
        }) {
    std::move(future_review_log)
        .Transform([output = review_log_](gc::Root<FlashcardReviewLog> input) {
          output->Set(input.ptr());
          return futures::Past(Success());
        })
        .ConsumeErrors([output = review_log_](Error error) {
          output->Set(ValueOrError<gc::Ptr<FlashcardReviewLog>>(error));
          return futures::Past(EmptyValue{});
        });
  }

  const gc::Ptr<OpenBuffer>& buffer() const { return buffer_; }
  SingleLine answer() { return answer_; }
  SingleLine hint() { return hint_; }

  futures::ListenableValue<gc::Ptr<OpenBuffer>> card_front_buffer() const {
    return card_front_buffer_.get();
  }

  futures::ValueOrError<gc::Ptr<FlashcardReviewLog>> review_log() {
    return review_log_->NewFuture().Transform(
        [input = review_log_](EmptyValue) {
          return futures::Past(input->Get().value());
        });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output =
        *object_metadata_->lock();
    output.push_back(buffer_.object_metadata());
    if (std::optional<ValueOrError<gc::Ptr<FlashcardReviewLog>>> optional_log =
            review_log_->Get();
        optional_log.has_value())
      std::visit(overload{IgnoreErrors{},
                          [&output](gc::Ptr<FlashcardReviewLog> log) {
                            output.push_back(log.object_metadata());
                          }},
                 optional_log.value());
    return output;
  }
};
}  // namespace afc::editor

namespace afc::vm {
template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<editor::Flashcard>>>::object_type_name =
    types::ObjectName{IDENTIFIER_CONSTANT(L"Flashcard")};

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<NonNull<std::shared_ptr<editor::Flashcard>>>>>>>::
    object_type_name =
        types::ObjectName(IDENTIFIER_CONSTANT(L"VectorFlashcard"));
}  // namespace afc::vm
namespace afc::editor {
void RegisterFlashcard(language::gc::Pool& pool, vm::Environment& environment) {
  gc::Root<vm::ObjectType> flashcard_object_type = vm::ObjectType::New(
      pool,
      vm::VMTypeMapper<NonNull<std::shared_ptr<Flashcard>>>::object_type_name);

  environment.DefineType(flashcard_object_type.ptr());

  environment.Define(
      IDENTIFIER_CONSTANT(L"Flashcard"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [&pool](gc::Ptr<OpenBuffer> buffer, LazyString value)
                          -> ValueOrError<NonNull<std::shared_ptr<Flashcard>>> {
                        DECLARE_OR_RETURN(Flashcard flashcard,
                                          Flashcard::New(buffer, value));
                        return MakeNonNullShared<Flashcard>(
                            std::move(flashcard));
                      }));

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return flashcard->buffer().ToRoot();
                      })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"hint"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return ToLazyString(flashcard->hint());
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"answer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return ToLazyString(flashcard->answer());
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"review_buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard)
                          -> futures::ValueOrError<gc::Ptr<OpenBuffer>> {
                        return flashcard->review_log().Transform(
                            [](const gc::Ptr<FlashcardReviewLog>& log) {
                              return futures::Past(Success(log->buffer()));
                            });
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"card_front_buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return flashcard->card_front_buffer().ToFuture();
                      })
          .ptr());

  vm::container::Export<std::vector<NonNull<std::shared_ptr<Flashcard>>>>(
      pool, environment);

#if 0
  environment.Define(
      IDENTIFIER_CONSTANT(L"VectorFlashcard"),
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](NonNull<
                 std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>
                 buffers,
             Number default_predicted_recall_score) {
            return MakeNonNullShared<
                Protected<std::vector<NonNull<std::shared_ptr<Flashcard>>>>>(
                MakeProtected(buffers->lock(
                    [&default_predicted_recall_score](
                        std::vector<gc::Ptr<OpenBuffer>>& buffers_data) {
                      return container::MaterializeVector(
                          buffers_data |
                          std::views::transform(
                              [&default_predicted_recall_score](
                                  gc::Ptr<OpenBuffer> buffer) {
                                return MakeNonNullShared<Flashcard>(Flashcard{
                                    .buffer = buffer,
                                    .predicted_recall_score =
                                        default_predicted_recall_score});
                              }));
                    })));
          }));
#endif
}
}  // namespace afc::editor
