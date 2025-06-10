#include "buffer_load.cc"
#include "buffer_output.cc"
#include "lib/markdown.cc"

namespace flashcards {
namespace internal {
void SetScoreAndClose(Buffer buffer, Flashcard card, string score) {
  card.SetScore(score);
  buffer.Close();
}
}  // namespace internal

VectorFlashcard PickFlashcards(string reviews_directory) {
  string cloze_tag = "cloze";
  Buffer log_buffer = OutputBuffer("/tmp/reviews/log");
  VectorFileTags buffers =
      LoadFileTagsFromGlob(reviews_directory + "/???.md", log_buffer)
          .filter([](FileTags tags) -> bool {
            return !tags.get(cloze_tag).empty();
          });
  OutputBufferLog(log_buffer,
                  "Buffers with flashcards: " + buffers.size().tostring());
  VectorFlashcard output;
  buffers.ForEach([](FileTags buffer) -> void {
    buffer.get(cloze_tag).ForEach([](string value) -> void {
      output.push_back(Flashcard(buffer.buffer(), value));
    });
  });
  OutputBufferLog(log_buffer, "Flashcards: " + output.size().tostring());
  return output;
}
}  // namespace flashcards

// TODO(2025-06-10, easy): Move this into the flashcards namespace.
// That requires improving src/flashcard.cc to find it there, which requires
// improving src/execution_context.h to be able to handle namespaces.
void ConfigureFrontCardBuffer(Buffer buffer, Flashcard card) {
  buffer.ApplyTransformation(
      InsertTransformationBuilder().set_text("Front").build());
  buffer.AddBinding(" ", "Flashcard: Show answer", []() -> void {
    buffer.Close();
    card.card_back_buffer();
  });
}

void ConfigureBackCardBuffer(Buffer buffer, Flashcard card) {
  buffer.AddBinding("1", "Flashcard: Failed", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
  buffer.AddBinding("2", "Flashcard: Hard", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
  buffer.AddBinding("3", "Flashcard: Good", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
  buffer.AddBinding("4", "Flashcard: Easy", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
  buffer.AddBinding(" ", "Flashcard: Good", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
}
