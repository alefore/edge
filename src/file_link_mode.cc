#include "src/file_link_mode.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/directory_listing.h"
#include "src/editor.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/tracker.h"
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/run_command_handler.h"
#include "src/search_handler.h"
#include "src/server.h"
#include "src/tests/tests.h"
#include "src/vm/callbacks.h"
#include "src/vm/value.h"

namespace execution = afc::infrastructure::execution;
namespace gc = afc::language::gc;

using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::concurrent::WorkQueue;
using afc::futures::IterationControlCommand;
using afc::futures::UnwrapVectorFuture;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::ProcessId;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IfObj;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
namespace {

futures::ValueOrError<ResolvePathOptions::ValidatorOutput> CanStatPath(
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    std::function<language::PossibleError(struct stat)> stat_validator,
    const infrastructure::Path& path) {
  VLOG(5) << "Considering path: " << path;
  return file_system_driver->Stat(path)
      .Transform(stat_validator)
      .Transform(
          [](EmptyValue) -> ValueOrError<ResolvePathOptions::ValidatorOutput> {
            VLOG(5) << "Path stat succeeded.";
            return Success(ResolvePathOptions::ValidatorOutput{});
          });
}

futures::Value<PossibleError> GenerateContents(
    NonNull<std::shared_ptr<struct stat>> stat_buffer,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    OpenBuffer& target) {
  CHECK(target.disk_state() == OpenBuffer::DiskState::kCurrent);
  FUTURES_ASSIGN_OR_RETURN(Path path,
                           Path::New(target.Read(buffer_variables::path)));
  LOG(INFO) << "GenerateContents: " << path;
  return file_system_driver->Stat(path).Transform(
      [stat_buffer, file_system_driver, &target,
       path](std::optional<struct stat> stat_results) {
        if (!stat_results.has_value()) {
          return futures::Past(Success());
        }
        stat_buffer.value() = stat_results.value();

        if (!S_ISDIR(stat_buffer->st_mode))
          return target.SetInputFromPath(path);
        return GenerateDirectoryListing(path, target).Transform([](EmptyValue) {
          return futures::Past(Success());
        });
      });
}

void HandleVisit(const struct stat& stat_buffer, const OpenBuffer& buffer) {
  const LazyString path = buffer.Read(buffer_variables::path);
  if (stat_buffer.st_mtime == 0) {
    LOG(INFO) << "Skipping file change check.";
    return;
  }

  LOG(INFO) << "Checking if file has changed: " << path;
  const std::string path_raw = path.ToBytes();
  struct stat current_stat_buffer;
  if (stat(path_raw.c_str(), &current_stat_buffer) == -1) {
    return;
  }
  if (current_stat_buffer.st_mtime <= stat_buffer.st_mtime) {
    return;
  }
  if (S_ISDIR(stat_buffer.st_mode)) {
    buffer.status().SetInformationText(Line{SingleLine{
        LazyString{L"🌷Directory changed in disk since last read."}}});
  } else {
    buffer.status().InsertError(
        Error{LazyString{L"🌷File changed in disk since last read."}});
  }
}

futures::Value<PossibleError> Save(
    EditorState& editor, NonNull<std::shared_ptr<struct stat>> stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  FUTURES_ASSIGN_OR_RETURN(
      Path immediate_path,
      AugmentError(
          LazyString{L"Buffer can't be saved: Invalid “path” variable"},
          Path::New(options.buffer->Read(buffer_variables::path))));

  futures::ValueOrError<Path> path_future = futures::Past(immediate_path);

  if (S_ISDIR(stat_buffer->st_mode)) {
    return options.save_type == OpenBuffer::Options::SaveType::kBackup
               ? futures::Past(Success())
               : futures::Past(PossibleError(Error{LazyString{
                     L"Buffer can't be saved: Buffer is a directory."}}));
  }

  switch (options.save_type) {
    case OpenBuffer::Options::SaveType::kMainFile:
      break;
    case OpenBuffer::Options::SaveType::kBackup:
      path_future =
          OnError(options.buffer->GetEdgeStateDirectory(), [](Error error) {
            return futures::Past(
                AugmentError(LazyString{L"Unable to backup buffer"}, error));
          }).Transform([](Path state_directory) {
            return Success(Path::Join(state_directory,
                                      PathComponent::FromString(L"backup")));
          });
  }

  return std::move(path_future)
      .Transform([&editor, stat_buffer, options](Path path) mutable {
        return SaveContentsToFile(path, options.buffer->contents().snapshot(),
                                  editor.thread_pool(),
                                  options.buffer->file_system_driver().value())
            .Transform([options](EmptyValue) mutable {
              return options.buffer->PersistState();
            })
            .Transform([&editor, stat_buffer, options, path](EmptyValue) {
              switch (options.save_type) {
                case OpenBuffer::Options::SaveType::kMainFile:
                  options.buffer->status().SetInformationText(LineBuilder{
                      SINGLE_LINE_CONSTANT(L"🖫 Saved: ") +
                      SingleLine{path.read()}}.Build());
                  for (const auto& dir : editor.edge_path()) {
                    options.buffer->execution_context()->EvaluateFile(
                        Path::Join(dir, ValueOrDie(Path::New(LazyString{
                                            L"hooks/buffer-save.cc"}))));
                  }
                  stat(path.ToBytes().c_str(), &stat_buffer.value());
                  break;
                case OpenBuffer::Options::SaveType::kBackup:
                  break;
              }
              return futures::Past(Success());
            });
      });
}

futures::Value<PossibleError> SaveContentsToOpenFile(
    ThreadPoolWithWorkQueue& thread_pool, Path original_path, Path path,
    FileDescriptor fd, LineSequence contents) {
  return thread_pool.Run([contents, original_path, path, fd]() {
    LOG(INFO) << original_path
              << ": SaveContentsToOpenFile: writing contents: " << path;
    // TODO: It'd be significant more efficient to do fewer (bigger) writes.
    std::optional<PossibleError> error;
    contents.EveryLine([&](LineNumber position, const Line& line) {
      std::string str =
          (position == LineNumber(0) ? "" : "\n") + line.contents().ToBytes();
      if (write(fd.read(), str.c_str(), str.size()) == -1) {
        Error write_error{path.read() + LazyString{L": write failed: "} +
                          LazyString{std::to_wstring(fd.read())} +
                          LazyString{L": "} +
                          LazyString{FromByteString(strerror(errno))}};
        LOG(INFO) << original_path
                  << ": SaveContentsToOpenFile: Error: " << write_error;
        error = std::move(write_error);
        return false;
      }
      return true;
    });
    LOG(INFO) << original_path
              << ": SaveContentsToOpenFile: Writing done: " << path;
    return error.value_or(Success());
  });
}
}  // namespace

// Caller must ensure that file_system_driver survives until the future is
// notified.
futures::Value<PossibleError> SaveContentsToFile(
    const Path& path, LineSequence contents,
    ThreadPoolWithWorkQueue& thread_pool,
    FileSystemDriver& file_system_driver) {
  Path tmp_path = Path::Join(
      ValueOrDie(path.Dirname()),
      ValueOrDie(PathComponent::New(ValueOrDie(path.Basename()).read() +
                                    LazyString{L".tmp"})));
  return futures::OnError(
             file_system_driver.Stat(path),
             [](Error error) {
               LOG(INFO)
                   << "Ignoring stat error; maybe a new file is being created: "
                   << error;
               struct stat value;
               value.st_mode =
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
               return futures::Past(Success(value));
             })
      .Transform([path, &file_system_driver, tmp_path](struct stat stat_value) {
        return OnError(
            file_system_driver.Open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                                    stat_value.st_mode),
            [tmp_path](Error error) {
              LOG(INFO) << tmp_path << ": Error opening file: " << error;
              return futures::Past(error);
            });
      })
      .Transform([&thread_pool, path, contents = std::move(contents), tmp_path,
                  &file_system_driver](FileDescriptor fd) {
        CHECK_NE(fd.read(), -1);
        return OnError(SaveContentsToOpenFile(thread_pool, path, tmp_path, fd,
                                              contents),
                       [&file_system_driver, fd](Error error) {
                         file_system_driver.Close(fd);
                         return futures::Past(error);
                       })
            .Transform([&file_system_driver, fd](EmptyValue) {
              return file_system_driver.Close(fd);
            })
            .Transform([path, &file_system_driver, tmp_path](EmptyValue) {
              return file_system_driver.Rename(tmp_path, path);
            });
      });
}

namespace {
futures::Value<gc::Root<OpenBuffer>> GetSearchPathsBuffer(
    EditorState& editor_state, const Path& edge_path) {
  BufferName buffer_name{LazyString{L"- search paths"}};
  return VisitOptional(
             [](gc::Root<OpenBuffer> buffer) { return futures::Past(buffer); },
             [&] {
               return OpenOrCreateFile(
                          OpenFileOptions{
                              .editor_state = editor_state,
                              .name = buffer_name,
                              .path = ToLazyString(Path::Join(
                                  edge_path, ValueOrDie(Path::New(LazyString{
                                                 L"search_paths"})))),
                              .glob_behavior =
                                  OpenFileGlobBehavior::kLiteralPath,
                              .insertion_type =
                                  BuffersList::AddBufferType::kIgnore,
                              .use_search_paths = false})
                   .Transform([&editor_state](
                                  std::vector<gc::Root<OpenBuffer>> buffers) {
                     CHECK_EQ(buffers.size(), 1ul);
                     gc::Root<OpenBuffer> buffer = std::move(buffers[0]);
                     buffer->Set(buffer_variables::save_on_close, true);
                     buffer->Set(
                         buffer_variables::trigger_reload_on_buffer_write,
                         false);
                     buffer->Set(buffer_variables::show_in_buffers_list, false);
                     if (!editor_state.has_current_buffer()) {
                       editor_state.set_current_buffer(
                           buffer, CommandArgumentModeApplyMode::kFinal);
                     }
                     return buffer;
                   });
             },
             editor_state.buffer_registry().Find(buffer_name))
      .Transform([](gc::Root<OpenBuffer> buffer) {
        return buffer->WaitForEndOfFile().Transform(
            [buffer](EmptyValue) { return buffer; });
      });
}
}  // namespace

futures::Value<std::vector<Path>> GetSearchPaths(EditorState& editor_state) {
  auto search_paths = MakeNonNullShared<std::vector<Path>>(
      std::vector<Path>({Path::LocalDirectory()}));

  auto paths = editor_state.edge_path();
  return futures::ForEachWithCopy(
             paths.begin(), paths.end(),
             [&editor_state, search_paths](Path edge_path) {
               return GetSearchPathsBuffer(editor_state, edge_path)
                   .Transform([&editor_state, search_paths,
                               edge_path](gc::Root<OpenBuffer> buffer) {
                     std::ranges::copy(
                         buffer->contents().snapshot() |
                             std::views::transform([](const Line& line) {
                               return Path::New(line.contents().read());
                             }) |
                             language::view::SkipErrors |
                             std::views::transform(
                                 [&editor_state](const Path& path) {
                                   return editor_state.expand_path(path);
                                 }),
                         std::back_inserter(search_paths.value()));
                     return futures::IterationControlCommand::kContinue;
                   });
             })
      .Transform([search_paths](futures::IterationControlCommand) mutable {
        LOG(INFO) << "Got search paths: " << search_paths->size();
        std::ranges::for_each(search_paths.value(),
                              [](Path path) { LOG(INFO) << "Path: " << path; });
        return std::move(search_paths.value());
      });
}

futures::ValueOrError<ResolvePathOutput> FindAlreadyOpenBuffer(
    EditorState& editor_state, LazyString path) {
  TRACK_OPERATION(FindAlreadyOpenBuffer);
  if (path.empty()) return futures::Past(Success(ResolvePathOutput{}));
  return ResolvePath(
             ResolvePathOptions{
                 .path = path,
                 .home_directory = editor_state.home_directory(),
                 .validator = [&editor_state](const Path& path_to_validate)
                     -> futures::ValueOrError<
                         ResolvePathOptions::ValidatorOutput> {
                   TRACK_OPERATION(FindAlreadyOpenBuffer_InnerLoop);
                   if (std::vector<gc::Root<OpenBuffer>> buffers =
                           editor_state.buffer_registry()
                               .FindBuffersPathEndingIn(path_to_validate);
                       !buffers.empty()) {
                     return futures::Past(buffers.at(0));
                   }
                   return futures::Past(
                       Error{LazyString{L"Unable to find buffer"}});
                 }})
      .Transform([](ResolvePathOutput input)
                     -> futures::ValueOrError<ResolvePathOutput> {
        for (auto& entry : input.entries)
          if (entry.position.has_value()) {
            entry.validator_output.value()->set_position(
                entry.position.value());
          }
        // TODO: Apply pattern.
        return futures::Past(Success(std::move(input)));
      });
}

gc::Root<OpenBuffer> CreateBuffer(
    const OpenFileOptions& options,
    std::optional<ResolvePathOutput::Entry> resolve_path_output) {
  EditorState& editor_state = options.editor_state;
  const std::optional<Path> buffer_options_path =
      std::invoke([&resolve_path_output, &options] -> std::optional<Path> {
        if (resolve_path_output.has_value()) return resolve_path_output->path;
        return std::visit(
            overload{[](const Error&) -> std::optional<Path> {
                       return std::nullopt;
                     },
                     [&options](Path path) -> std::optional<Path> {
                       return options.editor_state.expand_path(path);
                     }},
            Path::New(options.path));
      });

  auto buffer_options =
      MakeNonNullShared<OpenBuffer::Options>(OpenBuffer::Options{
          .editor = options.editor_state,
          .name = options.name.has_value()
                      ? options.name.value()
                      : std::invoke([&editor_state, &buffer_options_path] {
                          return buffer_options_path.has_value()
                                     ? BufferName{BufferFileId{
                                           buffer_options_path.value()}}
                                     : editor_state.buffer_registry()
                                           .NewAnonymousBufferName();
                        }),
          .path = buffer_options_path});

  NonNull<std::shared_ptr<struct stat>> stat_buffer;
  auto file_system_driver =
      MakeNonNullShared<FileSystemDriver>(editor_state.thread_pool());

  buffer_options->generate_contents = [stat_buffer,
                                       file_system_driver](OpenBuffer& target) {
    return GenerateContents(stat_buffer, file_system_driver, target);
  };
  buffer_options->handle_visit = [stat_buffer](OpenBuffer& buffer) {
    HandleVisit(stat_buffer.value(), buffer);
  };
  buffer_options->handle_save =
      options.path.empty()
          ? std::function([](OpenBuffer::Options::HandleSaveOptions) {
              return futures::Past(Success());
            })
          : std::function(
                [&editor_state = options.editor_state, stat_buffer](
                    OpenBuffer::Options::HandleSaveOptions save_options)
                    -> futures::Value<PossibleError> {
                  return Save(editor_state, stat_buffer,
                              std::move(save_options));
                });
  buffer_options->log_supplier =
      [&editor_state = options.editor_state](Path edge_state_directory) {
        FileSystemDriver driver(editor_state.thread_pool());
        return NewFileLog(driver,
                          Path::Join(edge_state_directory,
                                     PathComponent::FromString(L".edge_log")));
      };

  gc::Root<OpenBuffer> buffer = VisitOptional(
      [&](Path) {
        return options.editor_state.buffer_registry().MaybeAdd(
            buffer_options->name, [&] {
              gc::Root<OpenBuffer> output =
                  OpenBuffer::New(buffer_options.value());
              output->Set(buffer_variables::persist_state, true);
              output->Reload();
              return output;
            });
      },
      [&] {
        gc::Root<OpenBuffer> output = OpenBuffer::New(buffer_options.value());
        output->Set(buffer_variables::persist_state, true);
        output->Reload();
        options.editor_state.buffer_registry().Add(output->name(),
                                                   output.ptr().ToWeakPtr());
        return output;
      },
      buffer_options->path);
  buffer->ResetMode();

  if (resolve_path_output.has_value() &&
      resolve_path_output->position.has_value()) {
    buffer->set_position(*resolve_path_output->position);
  }

  options.editor_state.AddBuffer(buffer, options.insertion_type);

  if (resolve_path_output.has_value() &&
      resolve_path_output->pattern.has_value()) {
    std::visit(
        overload{[&](LineColumn position) { buffer->set_position(position); },
                 [&buffer](Error error) {
                   buffer->status().SetInformationText(Line(
                       LineSequence::BreakLines(error.read()).FoldLines()));
                 }},
        GetNextMatch(options.editor_state.modifiers().direction,
                     SearchOptions{.starting_position = buffer->position(),
                                   .search_query = ToSingleLine(
                                       resolve_path_output->pattern.value())},
                     buffer->contents().snapshot()));
  }
  return buffer;
}

/* static */ ResolvePathOptions ResolvePathOptions::NewWithSearchPaths(
    EditorState& editor_state,
    language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
        file_system_driver,
    std::vector<infrastructure::Path> search_paths) {
  return ResolvePathOptions{
      .search_paths = std::move(search_paths),
      .home_directory = editor_state.home_directory(),
      .validator =
          std::bind_front(CanStatPath, file_system_driver,
                          [](struct stat) { return language::Success(); })};
}

namespace {
struct PathWithPosition {
  Path path;
  LineColumn position;

  static std::optional<PathWithPosition> New(Path path,
                                             std::vector<LazyString> inputs) {
    if (inputs.size() != 1 && inputs.size() != 2) return std::nullopt;
    LineColumn position = {};
    for (size_t i = 0; i < inputs.size(); ++i) {
      size_t value;
      try {
        value = stoi(inputs[i].ToString());
        if (value > 0) {
          value--;
        }
      } catch (const std::invalid_argument& ia) {
        LOG(INFO) << "stoi failed: invalid argument: " << inputs[i];
        return std::nullopt;
      } catch (const std::out_of_range& ia) {
        LOG(INFO) << "stoi failed: out of range: " << inputs[i];
        return std::nullopt;
      }
      if (i == 0) {
        position.line = LineNumber(value);
      } else {
        position.column = ColumnNumber(value);
      }
    }
    return PathWithPosition{.path = path, .position = position};
  }
};

std::ostream& operator<<(std::ostream& os, const PathWithPosition& p) {
  os << "[" << p.path << ":" << p.position.line << ":" << p.position.column
     << "]";
  return os;
}

struct PathWithPattern {
  Path path;
  NonEmptySingleLine pattern;

  static std::optional<PathWithPattern> New(Path path,
                                            std::vector<LazyString> inputs) {
    if (inputs.size() != 1 || !StartsWith(inputs[0], LazyString{L"/"}))
      return std::nullopt;
    if (std::optional<NonEmptySingleLine> pattern =
            OptionalFrom(NonEmptySingleLine::New(
                SingleLine::New(inputs[0].Substring(ColumnNumber{1}))));
        pattern.has_value()) {
      return PathWithPattern{.path = path, .pattern = pattern.value()};
    }
    return std::nullopt;
  }
};

std::ostream& operator<<(std::ostream& os, const PathWithPattern& p) {
  os << "[" << p.path << ":/" << p.pattern << "]";
  return os;
}

using ParsedPath = std::variant<Path, PathWithPosition, PathWithPattern>;

std::ostream& operator<<(std::ostream& os, const ParsedPath& v) {
  std::visit([&os](auto&& arg) { os << arg; }, v);
  return os;
}

const Path& GetPath(const ParsedPath& input) {
  return std::visit(
      overload{[](const Path& path) -> const Path& { return path; },
               [](const auto& obj) -> const Path& { return obj.path; }},
      input);
}

std::vector<ParsedPath> ParsePathSpec(Path search_path, LazyString input_path) {
  // We gradually peel off suffixes from input_path into suffixes. As we do
  // this, we append entries to output.
  std::vector<ParsedPath> output;
  ColumnNumberDelta str_end = input_path.size();
  std::vector<LazyString> suffixes = {};
  while (true) {
    std::visit(
        overload{
            IgnoreErrors{},
            [&](Path current_local_path) {
              Path resolved_full_path = std::invoke([&search_path,
                                                     &current_local_path] {
                Path full_path = Path::Join(search_path, current_local_path);
                return std::optional<Path>(OptionalFrom(full_path.Resolve()))
                    .value_or(full_path);
              });
              if (suffixes.empty())
                output.push_back(resolved_full_path);
              else if (std::optional<PathWithPattern> path_with_pattern =
                           PathWithPattern::New(resolved_full_path, suffixes);
                       path_with_pattern.has_value())
                output.push_back(path_with_pattern.value());
              else if (std::optional<PathWithPosition> path_with_position =
                           PathWithPosition::New(resolved_full_path, suffixes);
                       path_with_position.has_value())
                output.push_back(path_with_position.value());
              else
                LOG(INFO) << "Invalid parse: " << resolved_full_path << ": "
                          << suffixes.size() << ": " << input_path;
            }},
        Path::New(input_path.Substring(ColumnNumber{}, str_end)));
    if (suffixes.size() == 2) {
      CHECK(!output.empty());
      return output;
    }
    std::optional<ColumnNumber> new_colon = FindLastOf(
        input_path, {L':'}, ColumnNumber{} + str_end - ColumnNumberDelta{1});
    if (new_colon == std::nullopt) {
      CHECK(!output.empty());
      return output;
    }
    ColumnNumberDelta new_colon_delta = new_colon->ToDelta();
    CHECK_LT(new_colon_delta, str_end);
    suffixes.insert(suffixes.begin(),
                    input_path.Substring(
                        new_colon.value() + ColumnNumberDelta{1},
                        str_end - (new_colon_delta + ColumnNumberDelta{1})));
    str_end = new_colon_delta;
  }
}

const bool parse_path_spec_tests_registration = tests::Register(
    L"ParsePathSpec",
    {{.name = L"Simple",
      .callback =
          [] {
            auto output = ParsePathSpec(PathComponent::FromString(L"foo"),
                                        LazyString{L"bar"});
            CHECK_EQ(output.size(), 1ul);
            CHECK_EQ(ToLazyString(std::get<Path>(output[0])),
                     LazyString{L"foo/bar"});
          }},
     {.name = L"Pattern",
      .callback =
          [] {
            auto output = ParsePathSpec(PathComponent::FromString(L"foo"),
                                        LazyString{L"bar:/quux"});
            CHECK_EQ(output.size(), 2ul);
            std::invoke(
                [](PathWithPattern item) {
                  CHECK_EQ(ToLazyString(item.path), LazyString{L"foo/bar"});
                  CHECK_EQ(item.pattern,
                           NON_EMPTY_SINGLE_LINE_CONSTANT(L"quux"));
                },
                std::get<PathWithPattern>(output[1]));
          }},
     {.name = L"Multiple", .callback = [] {
        auto output = ParsePathSpec(PathComponent::FromString(L"foo"),
                                    LazyString{L"bar:quux::meh:25:43"});
        CHECK_EQ(output.size(), 3ul);

        CHECK_EQ(ToLazyString(std::get<Path>(output[0])),
                 LazyString{L"foo/bar:quux::meh:25:43"});

        std::invoke(
            [](PathWithPosition pos) {
              CHECK_EQ(ToLazyString(pos.path),
                       LazyString{L"foo/bar:quux::meh:25"});
              CHECK_EQ(pos.position, LineColumn{LineNumber{42}});
            },
            std::get<PathWithPosition>(output[1]));

        std::invoke(
            [](PathWithPosition pos) {
              CHECK_EQ(ToLazyString(pos.path),
                       LazyString{L"foo/bar:quux::meh"});
              CHECK_EQ(pos.position,
                       (LineColumn{LineNumber{24}, ColumnNumber{42}}));
            },
            std::get<PathWithPosition>(output[2]));
      }}});

futures::Value<std::vector<ResolvePathOutput::Entry>> GetEntriesInSearchPath(
    ResolvePathOptions input, Path search_path) {
  LOG(INFO) << "GetEntriesInSearchPath: " << search_path << ": " << input.path;
  return UnwrapVectorFuture(
             MakeNonNullShared<std::vector<
                 futures::Value<std::vector<ResolvePathOutput::Entry>>>>(
                 ParsePathSpec(search_path, input.path) |
                 std::views::transform([input](ParsedPath parse) {
                   VLOG(5) << "Running validator: " << parse;
                   return input.validator(GetPath(parse))
                       .Transform([parse](ResolvePathOptions::ValidatorOutput
                                              validator_output)
                                      -> ValueOrError<std::vector<
                                          ResolvePathOutput::Entry>> {
                         VLOG(4) << "Validtor done: " << parse;
                         return Success(std::vector{ResolvePathOutput::Entry{
                             .path = GetPath(parse),
                             .position =
                                 std::holds_alternative<PathWithPosition>(parse)
                                     ? std::make_optional(
                                           std::get<PathWithPosition>(parse)
                                               .position)
                                     : std::optional<LineColumn>(),
                             .pattern =
                                 std::holds_alternative<PathWithPattern>(parse)
                                     ? std::make_optional(
                                           std::get<PathWithPattern>(parse)
                                               .pattern)
                                     : std::optional<NonEmptySingleLine>(),
                             .validator_output = validator_output}});
                       })
                       .ConsumeErrors([](Error) {
                         return futures::Past(
                             std::vector<ResolvePathOutput::Entry>{});
                       });
                 }) |
                 std::ranges::to<std::vector>()))
      .Transform([](std::vector<std::vector<ResolvePathOutput::Entry>>
                        nested_outputs) {
        VLOG(5) << "Nested outputs: " << nested_outputs.size();
        return nested_outputs | std::views::join |
               std::ranges::to<std::vector>();
      });
}
}  // namespace

futures::Value<ResolvePathOutput> ResolvePath(ResolvePathOptions input) {
  LOG(INFO) << "Resolve path: " << input.path;
  if (input.path.empty()) return futures::Past(ResolvePathOutput{});

  if (find(input.search_paths.begin(), input.search_paths.end(),
           Path::LocalDirectory()) == input.search_paths.end()) {
    input.search_paths.push_back(Path::LocalDirectory());
  }

  std::visit(overload{IgnoreErrors{},
                      [&](Path path) {
                        input.path = ToLazyString(Path::ExpandHomeDirectory(
                            input.home_directory, path));
                      }},
             Path::New(input.path));
  if (StartsWith(input.path, LazyString{L"/"}))
    input.search_paths = {Path::Root()};

  return UnwrapVectorFuture(
             MakeNonNullShared<std::vector<
                 futures::Value<std::vector<ResolvePathOutput::Entry>>>>(
                 input.search_paths |
                 std::views::transform(
                     std::bind_front(GetEntriesInSearchPath, input)) |
                 std::ranges::to<std::vector>()))
      .Transform([](std::vector<std::vector<ResolvePathOutput::Entry>>
                        nested_entries) {
        return ResolvePathOutput{.entries = nested_entries | std::views::join |
                                            std::ranges::to<std::vector>()};
      });
}

futures::ValueOrError<std::vector<gc::Root<OpenBuffer>>> OpenFileIfFound(
    const OpenFileOptions& options) {
  return FindAlreadyOpenBuffer(options.editor_state, options.path)
      .Transform([options](ResolvePathOutput already_open_buffers) {
        if (!already_open_buffers.entries.empty()) {
          return futures::Past(Success(
              already_open_buffers.entries |
              std::views::transform([&options](
                                        ResolvePathOutput::Entry& entry) {
                gc::Root<OpenBuffer> buffer =
                    std::move(entry.validator_output.value());
                options.editor_state.AddBuffer(buffer, options.insertion_type);
                return buffer;
              }) |
              std::ranges::to<std::vector>()));
        }
        return (options.use_search_paths ? GetSearchPaths(options.editor_state)
                                         : futures::Past(std::vector<Path>()))
            .Transform([options](std::vector<Path> search_paths)
                           -> futures::ValueOrError<
                               std::vector<gc::Root<OpenBuffer>>> {
              search_paths.insert(search_paths.begin(),
                                  options.initial_search_paths.begin(),
                                  options.initial_search_paths.end());
              return ResolvePath(
                         ResolvePathOptions{
                             .path = options.path,
                             .search_paths = std::move(search_paths),
                             .home_directory =
                                 options.editor_state.home_directory(),
                             .validator = std::bind_front(
                                 CanStatPath,
                                 MakeNonNullShared<FileSystemDriver>(
                                     options.editor_state.thread_pool()),
                                 options.stat_validator)})
                  .Transform([options](ResolvePathOutput input)
                                 -> futures::ValueOrError<
                                     std::vector<gc::Root<OpenBuffer>>> {
                    if (input.entries.empty())
                      return futures::Past(
                          Error{LazyString{L"No files matched."}});
                    return futures::Past(
                        Success(input.entries |
                                std::views::transform(
                                    std::bind_front(&CreateBuffer, options)) |
                                std::ranges::to<std::vector>()));
                  });
            });
      });
}

futures::Value<std::vector<gc::Root<OpenBuffer>>> OpenOrCreateFile(
    const OpenFileOptions& options) {
  return OpenFileIfFound(options).ConsumeErrors([options](Error) {
    return futures::Past(std::vector{CreateBuffer(options, std::nullopt)});
  });
}

futures::Value<gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state) {
  return OpenOrCreateFile(
             OpenFileOptions{
                 .editor_state = editor_state,
                 .path = LazyString{},
                 .glob_behavior = OpenFileGlobBehavior::kLiteralPath,
                 .insertion_type = BuffersList::AddBufferType::kIgnore,
                 .use_search_paths = false})
      .Transform([](std::vector<gc::Root<OpenBuffer>> buffers) {
        CHECK_EQ(buffers.size(), 1ul);
        // Wait until we've fully evaluated buffer-reload.cc on the buffer.
        return buffers[0]->WaitForEndOfFile().Transform(
            [buffers](EmptyValue) { return buffers[0]; });
      });
}

namespace {
using ::operator<<;
using afc::infrastructure::execution::ExecutionEnvironment;
using afc::infrastructure::execution::ExecutionEnvironmentOptions;

class TestDriver {
  NonNull<std::unique_ptr<EditorState>> editor_ = EditorForTests(std::nullopt);

  std::vector<LazyString> paths_to_unlink_ = {};

  bool stop_ = false;
  ExecutionEnvironment execution_environment_ =
      ExecutionEnvironment(ExecutionEnvironmentOptions{
          .stop_check = [&]() { return stop_; },
          .get_next_alarm = [&] { return editor_->WorkQueueNextExecution(); },
          .on_signals = [] { LOG(FATAL) << "Unexpected signals received."; },
          .on_iteration =
              [&](execution::IterationHandler& handle) {
                editor_->ExecutionIteration(handle);
              }});

 public:
  ~TestDriver() {
    for (const LazyString& path : paths_to_unlink_)
      CHECK_NE(unlink(path.ToBytes().c_str()), -1);
  }

  LazyString NewTmpFile(const LineSequence& contents) {
    char* path = strdup("/tmp/edge-tests-buffersave-simplesave-XXXXXX");
    int tmp_fd = mkstemp(path);
    CHECK(tmp_fd != -1) << path << ": " << strerror(errno);
    LazyString path_str = LazyString{FromByteString(path)};
    paths_to_unlink_.push_back(path_str);
    free(path);

    std::ranges::for_each(
        contents | std::views::transform([](const Line& line) {
          return line.contents().read();
        }) | Intersperse(LazyString{L"\n"}),
        [tmp_fd](LazyString line) {
          std::string line_str = line.ToBytes();
          write(tmp_fd, line_str.c_str(), line_str.size());
        });
    close(tmp_fd);

    return path_str;
  }

  futures::Value<ValueOrError<gc::Root<OpenBuffer>>> OpenAndReadPath(
      LazyString path, std::optional<LineSequence> expected_content) {
    return OpenFileIfFound(OpenFileOptions{.editor_state = editor_.value(),
                                           .path = path,
                                           .use_search_paths = true})
        .Transform([expected_content](
                       std::vector<gc::Root<OpenBuffer>> buffers) {
          CHECK_EQ(buffers.size(), 1ul);
          gc::Root<OpenBuffer> buffer = std::move(buffers[0]);
          return buffer->WaitForEndOfFile().Transform([expected_content,
                                                       buffer](EmptyValue) {
            buffer->contents().ForEach(
                [](std::wstring line) { LOG(INFO) << "Read line: " << line; });
            if (expected_content.has_value()) {
              LOG(INFO) << "Validating, length: " << expected_content->size();
              CHECK_EQ(buffer->lines_size(), expected_content->size());
              CHECK(buffer->contents().snapshot().EveryLine(
                  [expected_content](LineNumber i, const Line& line) {
                    CHECK_EQ(line.contents().ToBytes(),
                             expected_content->at(i).contents().ToBytes());
                    return true;
                  }));
            }
            return futures::Past(Success(buffer));
          });
        });
  }

  PossibleError WaitFor(futures::Value<PossibleError> value) {
    while (!value.has_value()) editor_->ExecutePendingWork();
    return value.Get().value();
  }

  void Stop() {
    CHECK(!stop_);
    stop_ = true;
  }

  void Run() { execution_environment_.Run(); };
};

const bool buffer_positions_tests_registration = tests::Register(
    L"BufferSave",
    {{.name = L"Load", .callback = [] {
        TestDriver driver;
        LineSequence contents =
            LineSequence::ForTests({L"Alejandro", L"Forero"});
        driver.OpenAndReadPath(driver.NewTmpFile(contents), contents)
            .Transform([&](gc::Root<OpenBuffer> buffer) {
              buffer->editor().CloseBuffer(buffer.value());
              driver.Stop();
              return Success();
            });
        driver.Run();
      }}});

}  // namespace

}  // namespace afc::editor
