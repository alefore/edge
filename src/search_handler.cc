#include "search_handler.h"

#include <set>

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

// Returns all columns where the current line matches the pattern.
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

// Returns a vector with all positions matching input sorted in ascending order.
vector<LineColumn> PerformSearch(const SearchOptions& options,
                                 OpenBuffer* buffer) {
  using namespace afc::editor;
  vector<LineColumn> positions;

#if CPP_REGEX
  // TODO: Get rid of ToByteString. Ugh.
  std::regex pattern(
      ToByteString(options.search_query),
      options.case_sensitive ? 0 : std::regex_constants::icase);
#else
  regex_t pattern;
  int cflags = 0;
  if (!options.case_sensitive) {
    cflags |= REG_ICASE;
  }
  // TODO: Get rid of ToByteString. Ugh.
  if (regcomp(&pattern, ToByteString(options.search_query).c_str(),
              cflags) != 0) {
    return positions;
  }
#endif

  buffer->ForEachLine(
      [&positions, &pattern](size_t position, const Line& line) {
        for (const auto& column : GetMatches(line.ToString(), pattern)) {
          positions.push_back(LineColumn(position, column));
        }
        return true;
      });
  return positions;
}

}  // namespace

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

wstring RegexEscape(shared_ptr<LazyString> str) {
  wstring results;
  static wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  for (size_t i = 0; i < str->size(); i++) {
    wchar_t c = str->get(i);
    if (!iswalnum(c) && literal_characters.find(c) == wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  }
  return results;
}

// Returns all matches starting at start. If end is not nullptr, only matches
// in the region enclosed by start and *end will be returned.
vector<LineColumn> PerformSearchWithDirection(
    EditorState* editor_state, const SearchOptions& options) {
  auto buffer = editor_state->current_buffer()->second;
  auto direction = editor_state->modifiers().direction;
  vector<LineColumn> candidates = PerformSearch(options, buffer.get());
  if (direction == BACKWARDS) {
    std::reverse(candidates.begin(), candidates.end());
  }

  vector<LineColumn> head;
  vector<LineColumn> tail;

  if (options.has_limit_position) {
    auto start = min(options.starting_position, options.limit_position);
    auto end = max(options.starting_position, options.limit_position);
    LOG(INFO) << "Removing elements outside of the range: " << start
              << " to " << end;
    vector<LineColumn> valid_candidates;
    for (auto& candidate : candidates) {
      if (candidate >= start && candidate < end) {
        valid_candidates.push_back(candidate);
      }
    }
    candidates = std::move(valid_candidates);
  }

  // Split them into head and tail depending on the current direction.
  for (auto& candidate : candidates) {
    ((direction == FORWARDS
          ? candidate > options.starting_position
          : candidate < options.starting_position)
         ? head : tail)
        .push_back(candidate);
  }

  // Append the tail to the head.
  for (auto& candidate : tail) {
    head.push_back(candidate);
  }

  return head;
}

void SearchHandlerPredictor(
    EditorState* editor_state, const wstring& input,
    OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer()->second;
  SearchOptions options;
  options.search_query = input;
  options.case_sensitive = buffer->read_bool_variable(
      OpenBuffer::variable_search_case_sensitive());
  options.starting_position = buffer->position();
  auto positions = PerformSearchWithDirection(editor_state, options);

  // Get the first kMatchesLimit matches:
  const int kMatchesLimit = 100;
  std::set<wstring> matches;
  for (size_t i = 0; i < positions.size() && matches.size() < kMatchesLimit; i++) {
    if (i == 0) {
      buffer->set_position(positions[0]);
      editor_state->set_status_prompt(false);
      editor_state->ScheduleRedraw();
    }
    matches.insert(RegexEscape(
        buffer->LineAt(positions[i].line)->Substring(positions[i].column)));
  }

  // Add the matches to the predictions buffer.
  for (auto& match : matches) {
    predictions_buffer->AppendToLastLine(editor_state, NewCopyString(match));
    predictions_buffer->AppendRawLine(editor_state,
                                      std::make_shared<Line>(Line::Options()));
  }
  predictions_buffer->EndOfFile(editor_state);
}

vector<LineColumn> SearchHandler(
    EditorState* editor_state, const SearchOptions& options) {
  editor_state->set_last_search_query(options.search_query);
  if (!editor_state->has_current_buffer() || options.search_query.empty()) {
    return {};
  }

  return PerformSearchWithDirection(editor_state, options);
}

void JumpToNextMatch(
    EditorState* editor_state, const SearchOptions& options) {
  auto results = SearchHandler(editor_state, options);
  if (results.empty()) {
    editor_state->SetStatus(L"No matches: " + options.search_query);
  } else {
    editor_state->current_buffer()->second->set_position(results[0]);
    editor_state->PushCurrentPosition();
  }
}

}  // namespace editor
}  // namespace afc
