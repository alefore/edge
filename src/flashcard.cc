#include "src/flashcard.h"

#include <vector>

#include "src/buffer.h"
#include "src/buffer_vm.h"
#include "src/concurrent/protected.h"
#include "src/file_link_mode.h"
#include "src/file_tags.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/lazy_value.h"
#include "src/language/observers.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
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
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::GetValueOrNullOpt;
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
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::math::numbers::Number;

namespace afc::vm {
// TODO(trivial, 2025-06-10): This probably should be moved to a central
// location and redundant logic in src/buffer_vm.cc should be deduplicated
// against it.
template <typename T>
struct VMTypeMapper<gc::Ptr<T>> {
  static gc::Ptr<T> get(Value& value) {
    return value.get_user_value<gc::Ptr<T>>(object_type_name).value();
  }

  static gc::Root<Value> New(gc::Pool& pool, gc::Ptr<T> value) {
    auto shared_value = MakeNonNullShared<gc::Ptr<T>>(value);
    return vm::Value::NewObject(
        pool, object_type_name, shared_value, [shared_value] {
          return std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>{
              {shared_value->object_metadata()}};
        });
  }

  static gc::Root<Value> New(gc::Pool& pool, gc::Root<T> value) {
    return New(pool, value.ptr());
  }

  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm
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

  enum class Score { kFail, kHard, kGood, kEasy };
  void SetScore(Score) {
    // TODO(2025-06-10, easy): Implement: propagate value to file_tags_.
  }

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

LineSequence PrepareCardContents(LineSequence original, SingleLine answer,
                                 SingleLine answer_cover) {
  LOG(INFO) << "Preparing card contents.";
  bool found_end_marker = false;
  return original.Map([&](Line input_line) {
    SingleLine input = input_line.contents();
    static const std::unordered_set<SingleLine> end_markers = {
        SINGLE_LINE_CONSTANT(L"## Related"), SINGLE_LINE_CONSTANT(L"## Tags")};
    if (found_end_marker || end_markers.contains(input)) {
      found_end_marker = true;
      return Line{};
    }

    LineBuilder output;
    // This is quite inefficient… maybe we should optimize it.
    for (ColumnNumber index; index.ToDelta() < input.size(); ++index) {
      if (StartsWith(input.Substring(index), answer)) {
        output.AppendString(
            answer_cover,
            LineModifierSet{LineModifier::kCyan, LineModifier::kReverse});
        index += answer.size();
      } else
        output.AppendCharacter(input.get(index), {});
    }
    LOG(INFO) << "Finished building.";
    return std::move(output).Build();
  });
}

class Flashcard {
  struct ConstructorAccessTag {};

  const gc::Ptr<OpenBuffer> buffer_;

  // LazyValue<> objects only retain a gc::Ptr<> (rather than gc::Root<>) to the
  // objects they create. However, they must ensure that the pointers are
  // exposed through `Expand` to the pool *before releasing the roots*. We use
  // object_metadata_ for that purpose. Otherwise, if Expand checked directly on
  // the LazyValue instances, there would be a race (the root is already
  // deleted, but the LazyValue doesn't yet return its Ptr).
  NonNull<std::shared_ptr<
      Protected<std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>>>>
      object_metadata_;

  const SingleLine answer_;
  const SingleLine hint_;

  // futures::Value has specially handling of ValueOrError which gets in our
  // way, so we wrap it to disable that.
  //
  // TODO(2025-06-10, easy): Find a better way to make it possible to create a
  // ListenableValue<ValueOrError<T>> directly.
  template <typename T>
  struct InternalValueOrErrorWrapper {
    ValueOrError<T> value_or_error;
  };
  const futures::ListenableValue<
      InternalValueOrErrorWrapper<gc::Ptr<FlashcardReviewLog>>>
      review_log_;

  const LazyValue<futures::ListenableValue<gc::Ptr<OpenBuffer>>>
      card_front_buffer_;

  const LazyValue<futures::ListenableValue<gc::Ptr<OpenBuffer>>>
      card_back_buffer_;

  gc::WeakPtr<Flashcard> ptr_this_;

 public:
  static ValueOrError<gc::Root<Flashcard>> New(gc::Ptr<OpenBuffer> buffer,
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

    EditorState& editor = buffer->editor();
    gc::Root<Flashcard> output =
        editor.gc_pool().NewRoot(MakeNonNullUnique<Flashcard>(
            ConstructorAccessTag{}, std::move(buffer), answer,
            tokens[1].value.read(),
            FlashcardReviewLog::New(editor, review_log_path, answer)));
    output->ptr_this_ = output.ptr().ToWeakPtr();
    return output;
  }

  Flashcard(
      ConstructorAccessTag, gc::Ptr<OpenBuffer> buffer, SingleLine answer,
      SingleLine hint,
      futures::ValueOrError<gc::Root<FlashcardReviewLog>> future_review_log)
      : buffer_(std::move(buffer)),
        object_metadata_(
            MakeNonNullShared<Protected<
                std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>>>(
                MakeProtected(
                    std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>{
                        buffer_.object_metadata()}))),
        answer_(std::move(answer).read()),
        hint_(std::move(hint).read()),
        review_log_(futures::ListenableValue(
            std::move(future_review_log)
                .Transform([protected_object_metadata = object_metadata_](
                               gc::Root<FlashcardReviewLog> input) {
                  protected_object_metadata->lock(
                      [&input](std::vector<
                               NonNull<std::shared_ptr<gc::ObjectMetadata>>>&
                                   object_metadata) {
                        object_metadata.push_back(
                            input.ptr().object_metadata());
                      });
                  return futures::Past(Success(
                      InternalValueOrErrorWrapper<gc::Ptr<FlashcardReviewLog>>{
                          .value_or_error = input.ptr()}));
                })
                .ConsumeErrors([](Error error) {
                  return futures::Past(
                      InternalValueOrErrorWrapper<gc::Ptr<FlashcardReviewLog>>{
                          error});
                }))),
        card_front_buffer_(std::bind_front(&Flashcard::PrepareCardBuffer, this,
                                           CardType::kFront)),
        card_back_buffer_(std::bind_front(&Flashcard::PrepareCardBuffer, this,
                                          CardType::kBack)) {}

  Flashcard(const Flashcard&) = delete;
  Flashcard(Flashcard&&) = delete;
  Flashcard& operator=(const Flashcard&) = delete;
  Flashcard& operator=(Flashcard&&) = delete;

  const gc::Ptr<OpenBuffer>& buffer() const { return buffer_; }
  SingleLine answer() { return answer_; }
  SingleLine hint() { return hint_; }

  futures::ListenableValue<gc::Ptr<OpenBuffer>> card_front_buffer() const {
    return card_front_buffer_.get();
  }

  futures::ListenableValue<gc::Ptr<OpenBuffer>> card_back_buffer() const {
    return card_back_buffer_.get();
  }

  futures::ValueOrError<gc::Ptr<FlashcardReviewLog>> review_log() {
    return review_log_.ToFuture().Transform(
        [](auto value) { return std::move(value.value_or_error); });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    return *object_metadata_->lock();
  }

 private:
  enum class CardType { kFront, kBack };
  futures::ListenableValue<gc::Ptr<OpenBuffer>> PrepareCardBuffer(
      CardType card_type) {
    LOG(INFO) << "Starting computation of card: "
              << (card_type == CardType::kFront ? "front" : "back");
    static const SingleLine kPadding = SINGLE_LINE_CONSTANT(L"  ");
    LineSequence card_contents = PrepareCardContents(
        buffer_->contents().snapshot(), answer_,
        card_type == CardType::kFront ? (kPadding + hint_ + kPadding)
                                      : answer_);
    return futures::ListenableValue(
        OpenAnonymousBuffer(buffer_->editor())
            .Transform([card_type, card_contents,
                        protected_object_metadata = object_metadata_,
                        weak_this =
                            ptr_this_](gc::Root<OpenBuffer> output_buffer) {
              LOG(INFO) << "Received anonymous buffer.";
              output_buffer->Set(buffer_variables::allow_dirty_delete, true);
              output_buffer->Set(buffer_variables::persist_state, false);
              output_buffer->InsertInPosition(
                  LineSequence::WithLine(
                      Line{SINGLE_LINE_CONSTANT(L"## Flashcard")}) +
                      card_contents,
                  LineColumn{}, std::nullopt);
              VisitOptional(
                  [&](gc::Root<Flashcard> root_this) {
                    std::visit(
                        overload{
                            [](Error error) { LOG(INFO) << error; },
                            [](ExecutionContext::CompilationResult result) {
                              result.evaluate();
                            }},
                        output_buffer->execution_context()->FunctionCall(
                            card_type == CardType::kFront
                                ? IDENTIFIER_CONSTANT(
                                      L"ConfigureFrontCardBuffer")
                                : IDENTIFIER_CONSTANT(
                                      L"ConfigureBackCardBuffer"),
                            {vm::VMTypeMapper<gc::Ptr<OpenBuffer>>::New(
                                 output_buffer.pool(), output_buffer.ptr())
                                 .ptr(),
                             vm::VMTypeMapper<gc::Ptr<Flashcard>>::New(
                                 output_buffer.pool(), root_this.ptr())
                                 .ptr()}));
                  },
                  [] {}, weak_this.Lock());
              protected_object_metadata->lock(
                  [&output_buffer](
                      std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>&
                          object_metadata) {
                    object_metadata.push_back(
                        output_buffer.ptr().object_metadata());
                  });
              output_buffer->editor().AddBuffer(
                  output_buffer, BuffersList::AddBufferType::kVisit);
              return output_buffer.ptr();
            }));
  }
};
}  // namespace afc::editor

namespace afc::vm {
template <>
const types::ObjectName
    VMTypeMapper<gc::Ptr<editor::Flashcard>>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"Flashcard")};

template <>
struct VMTypeMapper<gc::Root<editor::Flashcard>> {
  static gc::Root<Value> New(gc::Pool& pool,
                             gc::Root<editor::Flashcard> value) {
    return VMTypeMapper<gc::Ptr<editor::Flashcard>>::New(pool,
                                                         std::move(value));
  }
  static const types::ObjectName object_type_name;
};

template <>
const types::ObjectName
    VMTypeMapper<gc::Root<editor::Flashcard>>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"Flashcard")};

template <>
struct VMTypeMapper<NonNull<
    std::shared_ptr<Protected<std::vector<gc::Ptr<editor::Flashcard>>>>>> {
  static NonNull<
      std::shared_ptr<Protected<std::vector<gc::Ptr<editor::Flashcard>>>>>
  get(Value& value) {
    return value
        .get_user_value<Protected<std::vector<gc::Ptr<editor::Flashcard>>>>(
            object_type_name);
  }
  static gc::Root<Value> New(
      gc::Pool& pool,
      NonNull<
          std::shared_ptr<Protected<std::vector<gc::Ptr<editor::Flashcard>>>>>
          input) {
    return vm::Value::NewObject(pool, object_type_name, input,
                                [input] { return Expand(input); });
  }

  static gc::Root<Value> New(
      gc::Pool& pool,
      NonNull<
          std::shared_ptr<Protected<std::vector<gc::Root<editor::Flashcard>>>>>
          input) {
    return input->lock([&pool](std::vector<gc::Root<editor::Flashcard>> roots) {
      return VMTypeMapper<NonNull<std::shared_ptr<
          Protected<std::vector<gc::Ptr<editor::Flashcard>>>>>>::
          New(pool, MakeNonNullShared<
                        Protected<std::vector<gc::Ptr<editor::Flashcard>>>>(
                        MakeProtected(language::container::MaterializeVector(
                            roots | gc::view::Ptr))));
    });
  }
  static const types::ObjectName object_type_name;
};

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<gc::Ptr<editor::Flashcard>>>>>>::object_type_name =
    types::ObjectName(IDENTIFIER_CONSTANT(L"VectorFlashcard"));
}  // namespace afc::vm
namespace afc::editor {
void RegisterFlashcard(gc::Pool& pool, vm::Environment& environment) {
  gc::Root<vm::ObjectType> flashcard_object_type = vm::ObjectType::New(
      pool, vm::VMTypeMapper<gc::Ptr<Flashcard>>::object_type_name);

  environment.DefineType(flashcard_object_type.ptr());

  environment.Define(
      IDENTIFIER_CONSTANT(L"Flashcard"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [&pool](gc::Ptr<OpenBuffer> buffer, LazyString value)
                          -> ValueOrError<gc::Root<Flashcard>> {
                        return Flashcard::New(buffer, value);
                      }));

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](gc::Ptr<Flashcard> flashcard) {
                        return flashcard->buffer().ToRoot();
                      })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"hint"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](gc::Ptr<Flashcard> flashcard) {
                        return ToLazyString(flashcard->hint());
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"answer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](gc::Ptr<Flashcard> flashcard) {
                        return ToLazyString(flashcard->answer());
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"SetScore"),
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](gc::Ptr<Flashcard> flashcard,
             LazyString score_str) -> futures::ValueOrError<EmptyValue> {
            static const std::unordered_map<LazyString,
                                            FlashcardReviewLog::Score>
                scores = {
                    {LazyString{L"fail"}, FlashcardReviewLog::Score::kFail},
                    {LazyString{L"hard"}, FlashcardReviewLog::Score::kHard},
                    {LazyString{L"good"}, FlashcardReviewLog::Score::kGood},
                    {LazyString{L"easy"}, FlashcardReviewLog::Score::kEasy}};
            return VisitOptional(
                [flashcard](FlashcardReviewLog::Score score) {
                  return flashcard->review_log().Transform(
                      [score](const gc::Ptr<FlashcardReviewLog>& log) {
                        log->SetScore(score);
                        return futures::Past(Success());
                      });
                },
                [&] {
                  return futures::Past(
                      Error{LazyString{L"Invalid score: "} + score_str});
                },
                GetValueOrNullOpt(scores, score_str));
          })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"review_buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](gc::Ptr<Flashcard> flashcard)
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
                      [](gc::Ptr<Flashcard> flashcard) {
                        return flashcard->card_front_buffer().ToFuture();
                      })
          .ptr());

  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"card_back_buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](gc::Ptr<Flashcard> flashcard) {
                        return flashcard->card_back_buffer().ToFuture();
                      })
          .ptr());

  vm::container::Export<std::vector<gc::Ptr<Flashcard>>>(pool, environment);

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
