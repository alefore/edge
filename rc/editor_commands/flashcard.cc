#include "buffer_load.cc"
#include "buffer_output.cc"
#include "lib/dates.cc"
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

number AdjustInterval(number days_elapsed, number days_ideal,
                      string difficulty) {
  // When the actual and ideal interval are the same *and* the score is "good",
  // the next ideal interval grows by this amount.
  number kIntervalGrowthStep = 2;
  number kMaximumIntervalGrowthStep = 5;

  // TODO(easy, 2025-07-20): Compute the factor as a function of:
  //
  // - The relationship between days_elapsed and days_ideal. If elapsed is
  // larger, grow the factor more significantly (as elapsed / ideal goes to
  // infinity, factor should go to kMaximumIntervalGrowthStep).
  //
  // - The difficulty (fail should probably reset the interval to 10%? easy
  // should make the interval grow faster, hard slower).
  number factor = 2;

  return days_ideal * factor;
}

// Computes the number of days in which the user will repeat the flashcard in
// the ideal scenario.
number CurrentIdealIntervalDays(Flashcard card) {
  // Expected to contain a sorted list of strings of the form "YYYY-MM-DD
  // SCORE", where "SCORE" is one of {"fail", "easy", "good", "hard"}.
  VectorString reviews = FileTags(card.review_buffer()).get("Cloze");

  if (reviews.size() <= 1) return 1;

  number interval = 1;
  string previous_date = "";
  reviews.ForEach([](string value) -> void {
    number space = value.find_first_of(" ", 0);
    string date = value.substr(0, space);
    if (previous_date != "") {
      interval = AdjustInterval(Days(previous_date, date), interval,
                                value.substr(space, value.size()));
    }
    previous_date = date;
  });
  return interval;
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

// TODO(trivial, 2025-06-11): Expose a `Score` enum class somehow, rather than
// passing strings. If there's a mismatch, it should be detected at compile
// time.
void ConfigureBackCardBuffer(Buffer buffer, Flashcard card) {
  buffer.AddBinding("1", "Flashcard: Failed", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "fail");
  });
  buffer.AddBinding("2", "Flashcard: Hard", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "hard");
  });
  buffer.AddBinding("3", "Flashcard: Good", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "good");
  });
  buffer.AddBinding("4", "Flashcard: Easy", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "easy");
  });
  buffer.AddBinding(" ", "Flashcard: Good", []() -> void {
    flashcards::internal::SetScoreAndClose(buffer, card, "good");
  });
}
