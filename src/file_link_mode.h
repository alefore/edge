#ifndef __AFC_EDITOR_FILE_LINK_MODE_H__
#define __AFC_EDITOR_FILE_LINK_MODE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/text/line_sequence.h"
#include "src/widget_list.h"

namespace afc {
namespace editor {
// Saves the contents of the buffer to the path given.
futures::Value<language::PossibleError> SaveContentsToFile(
    const infrastructure::Path& path, language::text::LineSequence contents,
    concurrent::ThreadPoolWithWorkQueue& thread_pool,
    infrastructure::FileSystemDriver& file_system_driver);

struct OpenFileOptions {
  EditorState& editor_state;

  // Name can be absent, in which case the name will come from the path.
  std::optional<BufferName> name = std::nullopt;

  // The path of the file to open.
  std::optional<infrastructure::Path> path;

  BuffersList::AddBufferType insertion_type =
      BuffersList::AddBufferType::kVisit;

  // Should the contents of the search paths buffer be used to find the file?
  bool use_search_paths = true;
  std::vector<infrastructure::Path> initial_search_paths = {};

  // You can use this if you want to ignore specific files.
  std::function<language::PossibleError(struct stat)> stat_validator =
      [](struct stat) { return language::Success(); };
};

futures::Value<std::vector<infrastructure::Path>> GetSearchPaths(
    EditorState& editor_state);

// Takes a specification of a path (which can be absolute or relative) and, if
// relative, looks it up in the search paths. If a file is found, returns an
// absolute path pointing to it.
template <typename ValidatorOutput>
struct ResolvePathOptions {
 public:
  static futures::Value<ResolvePathOptions> New(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver) {
    return GetSearchPaths(editor_state)
        .Transform(std::bind_front(&NewWithSearchPaths, std::ref(editor_state),
                                   file_system_driver));
  }

  static ResolvePathOptions NewWithSearchPaths(
      EditorState& editor_state,
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver,
      std::vector<infrastructure::Path> search_paths = {}) {
    return ResolvePathOptions{
        .search_paths = std::move(search_paths),
        .home_directory = editor_state.home_directory(),
        .validator =
            std::bind_front(CanStatPath, file_system_driver,
                            [](struct stat) { return language::Success(); })};
  }

  // This is not a Path because it may contain various embedded tokens such as
  // a ':LINE:COLUMN' suffix. A Path will be extracted from it.
  std::wstring path = L"";
  std::vector<infrastructure::Path> search_paths = {};
  infrastructure::Path home_directory;

  using Validator = std::function<futures::ValueOrError<ValidatorOutput>(
      const infrastructure::Path&)>;
  Validator validator = nullptr;

  static futures::Value<language::PossibleError> CanStatPath(
      std::shared_ptr<infrastructure::FileSystemDriver> file_system_driver,
      std::function<language::PossibleError(struct stat)> stat_validator,
      const infrastructure::Path& path) {
    VLOG(5) << "Considering path: " << path;
    return file_system_driver->Stat(path).Transform(stat_validator);
  }
};

template <typename ValidatorOutput>
struct ResolvePathOutput {
  // The absolute path pointing to the file.
  infrastructure::Path path;

  // The position to jump to.
  std::optional<language::text::LineColumn> position;

  // The pattern to jump to (after jumping to `position`).
  std::optional<std::wstring> pattern;

  ValidatorOutput validator_output;
};

template <typename ValidatorOutput>
futures::ValueOrError<ResolvePathOutput<ValidatorOutput>> ResolvePath(
    ResolvePathOptions<ValidatorOutput> input) {
  using futures::IterationControlCommand;
  using futures::Past;
  using infrastructure::Path;
  using language::Error;
  using language::IgnoreErrors;
  using language::overload;
  using language::Success;
  using language::ValueOrError;
  using language::lazy_string::ColumnNumber;

  if (find(input.search_paths.begin(), input.search_paths.end(),
           Path::LocalDirectory()) == input.search_paths.end()) {
    input.search_paths.push_back(Path::LocalDirectory());
  }

  std::visit(
      overload{
          IgnoreErrors{},
          [&](Path path) {
            input.path =
                Path::ExpandHomeDirectory(input.home_directory, path).read();
          }},
      Path::FromString(input.path));
  if (!input.path.empty() && input.path[0] == L'/') {
    input.search_paths = {Path::Root()};
  }

  language::NonNull<std::shared_ptr<std::optional<
      language::ValueOrError<ResolvePathOutput<ValidatorOutput>>>>>
      output;
  return futures::ForEachWithCopy(
             input.search_paths.begin(), input.search_paths.end(),
             [input, output](Path search_path) {
               struct State {
                 const Path search_path;
                 size_t str_end;
               };
               auto state = std::make_shared<State>(
                   State{.search_path = std::move(search_path),
                         .str_end = input.path.size()});
               return futures::While([input, output, state]() {
                        if (state->str_end == input.path.npos ||
                            state->str_end == 0) {
                          return Past(IterationControlCommand::kStop);
                        }

                        ValueOrError<Path> input_path = Path::FromString(
                            input.path.substr(0, state->str_end));
                        if (IsError(input_path)) {
                          state->str_end =
                              input.path.find_last_of(':', state->str_end - 1);
                          return Past(IterationControlCommand::kContinue);
                        }
                        auto path_with_prefix =
                            Path::Join(state->search_path,
                                       ValueOrDie(std::move(input_path)));
                        CHECK(input.validator != nullptr);
                        return input.validator(path_with_prefix)
                            .Transform([input, output, state, path_with_prefix](
                                           ValidatorOutput validator_output) {
                              std::wstring output_pattern = L"";
                              std::optional<language::text::LineColumn>
                                  output_position;
                              for (size_t i = 0; i < 2; i++) {
                                while (state->str_end < input.path.size() &&
                                       ':' == input.path[state->str_end]) {
                                  state->str_end++;
                                }
                                if (state->str_end == input.path.size()) {
                                  break;
                                }
                                size_t next_str_end =
                                    input.path.find(':', state->str_end);
                                const std::wstring arg = input.path.substr(
                                    state->str_end, next_str_end);
                                if (i == 0 && arg.size() > 0 && arg[0] == '/') {
                                  output_pattern = arg.substr(1);
                                  break;
                                } else {
                                  size_t value;
                                  try {
                                    value = stoi(arg);
                                    if (value > 0) {
                                      value--;
                                    }
                                  } catch (const std::invalid_argument& ia) {
                                    LOG(INFO)
                                        << "stoi failed: invalid argument: "
                                        << arg;
                                    break;
                                  } catch (const std::out_of_range& ia) {
                                    LOG(INFO)
                                        << "stoi failed: out of range: " << arg;
                                    break;
                                  }
                                  if (!output_position.has_value()) {
                                    output_position =
                                        language::text::LineColumn();
                                  }
                                  if (i == 0) {
                                    output_position->line =
                                        language::text::LineNumber(value);
                                  } else {
                                    output_position->column =
                                        ColumnNumber(value);
                                  }
                                }
                                state->str_end = next_str_end;
                                if (state->str_end == input.path.npos) {
                                  break;
                                }
                              }
                              std::optional<Path> resolved_path =
                                  OptionalFrom(path_with_prefix.Resolve());
                              Path final_resolved_path =
                                  resolved_path.value_or(path_with_prefix);
                              output.value() =
                                  ResolvePathOutput<ValidatorOutput>{
                                      .path = final_resolved_path,
                                      .position = output_position,
                                      .pattern = output_pattern,
                                      .validator_output = validator_output};
                              VLOG(4)
                                  << "Resolved path: " << final_resolved_path;
                              return Success(IterationControlCommand::kStop);
                            })
                            .ConsumeErrors([state, input](Error) {
                              state->str_end = input.path.find_last_of(
                                  ':', state->str_end - 1);
                              return Past(IterationControlCommand::kContinue);
                            });
                      })
                   .Transform([output](IterationControlCommand) {
                     return output->has_value()
                                ? IterationControlCommand::kStop
                                : IterationControlCommand::kContinue;
                   });
             })
      .Transform([output](IterationControlCommand)
                     -> ValueOrError<ResolvePathOutput<ValidatorOutput>> {
        if (output->has_value()) return output->value();

        // TODO(easy): Give a better error. Perhaps include the paths in
        // which we searched? Perhaps the last result of the validator?
        return Error(L"Unable to resolve file.");
      });
}

futures::ValueOrError<language::gc::Root<OpenBuffer>> OpenFileIfFound(
    const OpenFileOptions& options);

futures::Value<language::gc::Root<OpenBuffer>> OpenOrCreateFile(
    const OpenFileOptions& options);

futures::Value<language::gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif
