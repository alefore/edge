#include "search_handler.h"

#include <iostream>
#if CPP_REGEX
#include <regex>
#else
extern "C" {
#include <sys/types.h>
#include <regex.h>
}
#endif

#include "char_buffer.h"
#include "editor.h"
#include "wstring.h"

namespace {

using namespace afc::editor;
using std::vector;
using std::wstring;

#if CPP_REGEX
typedef std::regex RegexPattern;
#else
typedef regex_t RegexPattern;
#endif

vector<size_t> GetMatches(const wstring& line, const RegexPattern& pattern) {
  size_t start = 0;
  vector<size_t> output;
  while (true) {
    size_t match = wstring::npos;
    // TODO: Ugh, our regexp engines are not wchar aware. :-(
    string line_substr = ToByteString(line.substr(min(start, line.size())));

#if CPP_REGEX
    std::smatch pattern_match;
    std::regex_search(line_substr, pattern_match, pattern);
    if (!pattern_match.empty()) {
      match = pattern_match.prefix().first - line_substr.begin();
    }
#else
    regmatch_t matches;
    if (regexec(&pattern, line_substr.c_str(), 1, &matches, 0) == 0) {
      match = matches.rm_so;
    }
#endif

    if (match == wstring::npos) { return output; }
    output.push_back(start + match);
    start += match + 1;
  }
}

size_t FindInterestingMatch(
    const vector<size_t> matches, bool wrapped,
    const BufferLineIterator& start_line, size_t start_column,
    const BufferLineIterator& line) {
  if (matches.empty()) { return wstring::npos; }
  if (line != start_line) { return *matches.begin(); }
  for (auto it = matches.begin(); it != matches.end(); ++it) {
    if (!wrapped ? *it > start_column : *it < start_column) {
      return *it;
    }
  }
  return wstring::npos;
}

size_t FindInterestingMatch(
    const vector<size_t> matches, bool wrapped,
    const BufferLineReverseIterator& start_line, size_t start_column,
    const BufferLineReverseIterator& line) {
  if (matches.empty()) { return wstring::npos; }
  if (line != start_line) { return *matches.begin(); }
  for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
    if (wrapped ? *it > start_column : *it < start_column) {
      return *it;
    }
  }
  return wstring::npos;
}

template <typename Iterator>
bool PerformSearch(
    const wstring& input, OpenBuffer* buffer, const Iterator& start_line,
    size_t start_column, const Iterator& begin, const Iterator& end,
    LineColumn* match_position, bool* wrapped) {
  using namespace afc::editor;

#if CPP_REGEX
  // TODO: Get rid of ToByteString. Ugh.
  std::regex pattern(ToByteString(input));
#else
  regex_t pattern;
  // TODO: Get rid of ToByteString. Ugh.
  if (regcomp(&pattern, ToByteString(input).c_str(), REG_ICASE) != 0) {
    return false;
  }
#endif

  Iterator line = start_line;

  *wrapped = false;

  while (true) {
    if (line.line() < buffer->end().line()) {
      wstring str = (*line)->ToString();

      vector<size_t> matches = GetMatches(str, pattern);
      size_t interesting_match;
      interesting_match =
          FindInterestingMatch(matches, *wrapped, start_line, start_column, line);

      if (interesting_match != wstring::npos) {
        match_position->line = line.line();
        match_position->column = interesting_match;
        return true;
      }
    }

    if (line == start_line && *wrapped) {
      return false;
    }
    if (line == end) {
      line = begin;
      *wrapped = true;
    } else {
      line++;
    }
  }
}

}  // namespace

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

shared_ptr<LazyString>
RegexEscape(shared_ptr<LazyString> str) {
  wstring results;
  static wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  for (size_t i = 0; i < str->size(); i++) {
    wchar_t c = str->get(i);
    if (!iswalnum(c) && literal_characters.find(c) == wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  }
  return NewCopyString(results);
}

bool PerformSearchWithDirection(
    EditorState* editor_state, const wstring& input, LineColumn* match_position,
    bool* wrapped) {
  auto buffer = editor_state->current_buffer()->second;
  if (editor_state->direction() == FORWARDS) {
    return PerformSearch(
        input, buffer.get(), buffer->line(), buffer->current_position_col(),
        buffer->begin(), buffer->end(), match_position, wrapped);
  }

  BufferLineReverseIterator rev_iterator(buffer->line());
  rev_iterator--;
  return PerformSearch(
      input, buffer.get(), rev_iterator, buffer->current_position_col(),
      buffer->rbegin(), buffer->rend(), match_position, wrapped);
}

void SearchHandlerPredictor(
    EditorState* editor_state, const wstring& input,
    OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer()->second;
  LineColumn match_position = buffer->position();
  bool already_wrapped = false;
  for (size_t i = 0; i < 10; i++) {
    bool wrapped;
    if (!PerformSearchWithDirection(
             editor_state, input, &match_position, &wrapped)) {
      break;
    }
    if (i == 0) {
      buffer->set_position(match_position);
      editor_state->set_status_prompt(false);
      editor_state->ScheduleRedraw();
    }
    predictions_buffer->AppendLine(
        editor_state,
        RegexEscape(
            buffer->LineAt(match_position.line)->Substring(
                match_position.column)));
    if (wrapped && already_wrapped) {
      break;
    }
    already_wrapped = already_wrapped || wrapped;
  }
  predictions_buffer->EndOfFile(editor_state);
}

void SearchHandler(
    const LineColumn& starting_position, const wstring& input,
    EditorState* editor_state) {
  editor_state->set_last_search_query(input);
  if (!editor_state->has_current_buffer() || input.empty()) {
    return;
  }

  auto buffer = editor_state->current_buffer()->second;
  if (starting_position != buffer->position()) {
    // The user must have used the predictor, which probably means we don't need
    // to do much.
    return;
  }

  LineColumn match_position;
  bool wrapped;

  if (!PerformSearchWithDirection(
           editor_state, input, &match_position, &wrapped)) {
    editor_state->SetStatus(L"No matches: " + input);
  } else {
    buffer->set_position(match_position);
    editor_state->PushCurrentPosition();
    if (wrapped) {
      editor_state->SetStatus(L"Found (wrapped).");
    } else {
      editor_state->SetStatus(L"Found.");
    }
  }
}

}  // namespace editor
}  // namespace afc
