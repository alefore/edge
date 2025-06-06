#include "src/flashcard.h"

#include <vector>

#include "src/buffer.h"
#include "src/buffer_vm.h"
#include "src/concurrent/protected.h"
#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"
#include "src/vm/container.h"
#include "src/vm/types.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::math::numbers::Number;

namespace afc::editor {
class Flashcard {
 private:
  gc::Ptr<OpenBuffer> buffer_;

  // Must be between 0 and 1.
  //
  // TODO(trivial, 2025-06-06): Use a GhostType?
  math::numbers::Number predicted_recall_score = Number::FromSizeT(0ul);

  SingleLine answer_;
  SingleLine hint_;

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

    return Flashcard(std::move(buffer), tokens[0].value.read(),
                     tokens[1].value.read());
  }

  Flashcard(gc::Ptr<OpenBuffer> buffer, SingleLine answer, SingleLine hint)
      : buffer_(std::move(buffer)),
        answer_(std::move(answer).read()),
        hint_(std::move(hint).read()) {}

  const gc::Ptr<OpenBuffer>& buffer() const { return buffer_; }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    return {buffer_.object_metadata()};
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
#if 0
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"set_buffer"),
      vm::NewCallback(pool, vm::kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard,
                         gc::Ptr<OpenBuffer> buffer) {
                        flashcard->buffer = buffer;
                        return flashcard;
                      })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"predicted_recall_score"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return flashcard->predicted_recall_score;
                      })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"set_predicted_recall_score"),
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](NonNull<std::shared_ptr<Flashcard>> flashcard, Number value)
              -> ValueOrError<NonNull<std::shared_ptr<Flashcard>>> {
            if (value < Number::FromSizeT(0) || value > Number::FromSizeT(1))
              return Error{LazyString{L"Invalid recall score."}};
            flashcard->predicted_recall_score = value;
            return flashcard;
          })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"source"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<Flashcard>> flashcard) {
                        return flashcard->source;
                      })
          .ptr());
  flashcard_object_type->AddField(
      IDENTIFIER_CONSTANT(L"set_source"),
      vm::NewCallback(
          pool, vm::kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Flashcard>> flashcard, LazyString source) {
            flashcard->source = source;
            return flashcard;
          })
          .ptr());
#endif
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
