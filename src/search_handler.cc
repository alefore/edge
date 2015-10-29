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

vector<size_t> FilterInterestingMatches(
    vector<size_t> matches, bool wrapped,
    const Tree<shared_ptr<Line>>::const_iterator& start_line,
    size_t start_column,
    const Tree<shared_ptr<Line>>::const_iterator& line) {
  if (matches.empty() || line != start_line) { return matches; }
  vector<size_t> output;
  for (auto it = matches.begin(); it != matches.end(); ++it) {
    if (!wrapped ? *it > start_column : *it < start_column) {
      output.push_back(*it);
    }
  }
  return output;
}

vector<size_t> FilterInterestingMatches(
    vector<size_t> matches, bool wrapped,
    const Tree<shared_ptr<Line>>::const_reverse_iterator& start_line,
    size_t start_column,
    const Tree<shared_ptr<Line>>::const_reverse_iterator& line) {
  if (matches.empty() || line != start_line) { return matches; }
  vector<size_t> output;
  for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
    if (wrapped ? *it > start_column : *it < start_column) {
      output.push_back(*it);
    }
  }
  return output;
}

template <typename Iterator>
vector<LineColumn> PerformSearch(
    const wstring& input, const Iterator& start_line,
    size_t start_column, const Iterator& first, const Iterator& last,
    bool* wrapped) {
  using namespace afc::editor;
  vector<LineColumn> positions;

#if CPP_REGEX
  // TODO: Get rid of ToByteString. Ugh.
  std::regex pattern(ToByteString(input));
#else
  regex_t pattern;
  // TODO: Get rid of ToByteString. Ugh.
  if (regcomp(&pattern, ToByteString(input).c_str(), REG_ICASE) != 0) {
    return positions;
  }
#endif

  Iterator line = start_line;

  *wrapped = false;

  while (true) {
    if (line < last) {
      wstring str = (*line)->ToString();

      vector<size_t> matches = FilterInterestingMatches(
          GetMatches(str, pattern), *wrapped, start_line, start_column, line);
      for (const auto& column : matches) {
        positions.push_back(LineColumn(line - first, column));
      }
    }

    if (line == start_line && *wrapped) {
      return positions;
    }
    if (line == last) {
      line = first;
      *wrapped = true;
    } else {
      ++line;
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

vector<LineColumn> PerformSearchWithDirection(
    EditorState* editor_state, const wstring& input, bool* wrapped) {
  auto buffer = editor_state->current_buffer()->second;
  auto position = buffer->position();
  if (editor_state->direction() == FORWARDS) {
    return PerformSearch(
        input, buffer->contents()->begin() + position.line,
        position.column, buffer->contents()->begin(), buffer->contents()->end(),
        wrapped);
  }

  auto result = PerformSearch(
      input, buffer->contents()->rend() - position.line,
      position.column, buffer->contents()->rbegin(), buffer->contents()->rend(),
      wrapped);
  for (auto& position : result) {
    position.line = buffer->contents()->size() - position.line - 1;
  }
  return result;
}

void SearchHandlerPredictor(
    EditorState* editor_state, const wstring& input,
    OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer()->second;
  LineColumn match_position = buffer->position();
  bool already_wrapped = false;
  for (size_t i = 0; i < 10; i++) {
    bool wrapped;
    auto positions = PerformSearchWithDirection(editor_state, input, &wrapped);
    if (positions.empty()) {
      break;
    }
    if (i == 0) {
      buffer->set_position(positions[0]);
      editor_state->set_status_prompt(false);
      editor_state->ScheduleRedraw();
    }
    predictions_buffer->AppendLine(
        editor_state,
        RegexEscape(
            buffer->LineAt(positions[0].line)->Substring(
                match_position.column)));
    if (wrapped && already_wrapped) {
      break;
    }
    already_wrapped = already_wrapped || wrapped;
  }
  predictions_buffer->EndOfFile(editor_state);
}

vector<LineColumn> SearchHandler(
    EditorState* editor_state, const SearchOptions& options) {
  editor_state->set_last_search_query(options.search_query);
  if (!editor_state->has_current_buffer() || options.search_query.empty()) {
    return vector<LineColumn>();
  }

  auto buffer = editor_state->current_buffer()->second;
  if (options.starting_position != buffer->position()) {
    // The user must have used the predictor, which probably means we don't need
    // to do much.
    return vector<LineColumn>();
  }

  bool wrapped;

  auto results = PerformSearchWithDirection(
      editor_state, options.search_query, &wrapped);
  if (results.empty()) {
    editor_state->SetStatus(L"No matches: " + options.search_query);
  } else {
    buffer->set_position(results[0]);
    editor_state->PushCurrentPosition();
    if (wrapped) {
      editor_state->SetStatus(L"Found (wrapped).");
    } else {
      editor_state->SetStatus(L"Found.");
    }
  }
  return results;
}

}  // namespace editor
}  // namespace afc
