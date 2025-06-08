#include "buffer_load.cc"
#include "buffer_output.cc"
#include "lib/markdown.cc"

namespace flashcards {
namespace internal {}  // namespace internal

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
