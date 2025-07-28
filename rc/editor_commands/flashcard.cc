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

VectorFlashcard PickFlashcards(Buffer log_buffer, string reviews_directory) {
  string cloze_tag = "cloze";
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
  if (difficulty == "fail") {
    number new_interval = days_ideal * 0.1;
    return new_interval > 1 ? new_interval : 1;
  }

  number kIntervalGrowthStep = 2.0;
  number kMaximumIntervalGrowthStep = 5.0;
  number E = 2.71828;  // Euler's number

  number difficulty_factor;
  if (difficulty == "easy") {
    difficulty_factor = 1.3;
  } else if (difficulty == "good") {
    difficulty_factor = 1.0;
  } else {  // hard
    difficulty_factor = 0.7;
  }

  number base_growth = kIntervalGrowthStep * difficulty_factor;

  number time_ratio = 1.0;
  if (days_ideal > 0) {
    time_ratio = days_elapsed / days_ideal;
  }

  number factor;
  if (time_ratio >= 1) {
    number excess_ratio = time_ratio - 1;
    // The bonus approaches (kMaximumIntervalGrowthStep - base_growth) as the
    // excess_ratio increases. The use of log ensures that the approach is
    // gradual.
    number bonus = (kMaximumIntervalGrowthStep - base_growth) *
                   (1 - 1 / log(excess_ratio + E));
    factor = base_growth + bonus;
  } else {
    // When reviewed early, the growth is penalized.
    // The growth factor is linearly interpolated between 1 (no growth) and
    // base_growth. time_ratio = 0 -> factor = 1. time_ratio = 1 -> factor =
    // base_growth.
    factor = 1 + (base_growth - 1) * time_ratio;
  }

  // Final sanity checks on the factor.
  if (factor > kMaximumIntervalGrowthStep) {
    factor = kMaximumIntervalGrowthStep;
  }
  if (factor < 1) {
    factor = 1;
  }

  return days_ideal * factor;
}

string TestAdjustInterval(number days_elapsed, number days_ideal,
                          string difficulty, number expected_min,
                          number expected_max) {
  number result = AdjustInterval(days_elapsed, days_ideal, difficulty);
  if (result >= expected_min && result <= expected_max) {
    return "";
  }
  return "Test failed: AdjustInterval(" + days_elapsed.tostring() + ", " +
         days_ideal.tostring() + ", " + difficulty +
         "). Expected result between " + expected_min.tostring() + " and " +
         expected_max.tostring() + ", but got " + result.tostring() + ". ";
}

string Validate() {
  string errors;

  // Test case 1: Difficulty "fail"
  errors += TestAdjustInterval(10, 100, "fail", 10, 10);
  errors += TestAdjustInterval(1000, 1000, "fail", 100, 100);
  errors += TestAdjustInterval(10, 5, "fail", 1, 1);

  // Test case 2: Difficulty "good", ideal == elapsed
  errors += TestAdjustInterval(10, 10, "good", 19.9, 20.1);

  // Test case 3: Difficulty "good", elapsed > ideal
  errors += TestAdjustInterval(20, 10, "good", 27.1, 27.2);

  // Test case 4: Difficulty "good", elapsed < ideal
  errors += TestAdjustInterval(5, 10, "good", 14.9, 15.1);

  // Test case 5: Difficulty "easy", ideal == elapsed
  errors += TestAdjustInterval(10, 10, "easy", 25.9, 26.1);

  // Test case 6: Difficulty "hard", ideal == elapsed
  errors += TestAdjustInterval(10, 10, "hard", 13.9, 14.1);

  // Test case 7: Maximum growth
  errors += TestAdjustInterval(1000, 10, "good", 43.5, 43.6);

  // Test case 8: Zero values
  errors += TestAdjustInterval(0, 10, "good", 9.9, 10.1);

  return errors;
}

// Computes the number of days in which the user will repeat the flashcard in
// the ideal scenario.
number CurrentIdealIntervalDays(FileTags review_tags) {
  // Expected to contain a sorted list of strings of the form "YYYY-MM-DD
  // SCORE", where "SCORE" is one of {"fail", "easy", "good", "hard"}.
  VectorString reviews = review_tags.get("Cloze");

  if (reviews.size() <= 1) return 1;

  number interval = 1;
  string previous_date = "";
  reviews.ForEach([](string value) -> void {
    number space = value.find_first_of(" ", 0);
    if (space == -1) {
      interval = 0;
      return;
    }
    string date = value.substr(0, space);
    if (previous_date != "") {
      interval =
          AdjustInterval(Days(previous_date, date), interval,
                         value.substr(space + 1, value.size() - (space + 1)));
    }
    previous_date = date;
  });
  return interval;
}

number DaysUntilNextReview(Flashcard card) {
  FileTags review_tags = FileTags(card.review_buffer());
  VectorString reviews = review_tags.get("Cloze");
  if (reviews.empty()) {
    return 0.01;  // No previous reviews, so it's due now.
  }

  number ideal_interval = CurrentIdealIntervalDays(review_tags);
  string last_review_line = reviews.get(reviews.size() - 1);
  number days_elapsed =
      Days(last_review_line.substr(0, last_review_line.find_first_of(" ", 0)),
           Today());
  return ideal_interval - days_elapsed;
}

void ReviewFlashcards(string directory, number cards_count) {
  Buffer log_buffer = OutputBuffer("/tmp/reviews/log");
  VectorFlashcard all_flashcards = PickFlashcards(log_buffer, directory);

  VectorFlashcard reviewable_cards =
      all_flashcards.filter([](Flashcard card) -> bool {
        FileTags review_tags = FileTags(card.review_buffer());
        VectorString reviews = review_tags.get("Cloze");
        if (reviews.empty()) {
          return true;
        }
        string last_review_line = reviews.get(reviews.size() - 1);
        string last_review_date =
            last_review_line.substr(0, last_review_line.find_first_of(" ", 0));
        return last_review_date != Today();
      });

  for (number i = 0; i < cards_count; i++) {
    if (reviewable_cards.empty()) return;

    Flashcard most_urgent_card = reviewable_cards.get(0);
    number min_days = DaysUntilNextReview(most_urgent_card);

    for (number j = 1; j < reviewable_cards.size(); j++) {
      Flashcard current_card = reviewable_cards.get(j);
      number current_days = DaysUntilNextReview(current_card);
      if (current_days < min_days) {
        min_days = current_days;
        most_urgent_card = current_card;
      }
    }

    OutputBufferLog(log_buffer,
                    most_urgent_card.buffer().name() +
                        "Days left for review: " + min_days.tostring());
    FileTags review_tags = FileTags(most_urgent_card.review_buffer());
    OutputBufferLog(log_buffer, "Cloze tags: " +
                                    review_tags.get("Cloze").size().tostring());

    most_urgent_card.card_front_buffer();

    Buffer buffer_to_remove = most_urgent_card.review_buffer();
    reviewable_cards = reviewable_cards.filter([](Flashcard card) -> bool {
      return card.review_buffer() != buffer_to_remove;
    });
  }
}
}  // namespace flashcards

// TODO(2025-06-10, easy): Move this into the flashcards namespace.
// That requires improving src/flashcard.cc to find it there, which requires
// improving src/execution_context.h to be able to handle namespaces.
void ConfigureFrontCardBuffer(Buffer buffer, Flashcard card) {
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
