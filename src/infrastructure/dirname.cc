#include "src/infrastructure/dirname.h"

#include <cstring>

extern "C" {
#include <fcntl.h>
#include <libgen.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::FindLastOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

namespace afc::infrastructure {

using ::operator<<;

/* static */ language::PossibleError PathComponentValidator::Validate(
    const LazyString& input) {
  if (input.empty()) return Error{LazyString{L"Component can't be empty."}};
  if (FindFirstOf(input, {L'/'}).has_value())
    return Error{LazyString{L"Component can't contain a slash: "} +
                 LazyString{input}};
  return Success();
}

/*  static */ PathComponent PathComponent::WithExtension(
    const PathComponent& path, const LazyString& extension) {
  return PathComponent(
      VisitOptional(
          [&path](ColumnNumber index) {
            return path.read().Substring(ColumnNumber{}, index.ToDelta());
          },
          [&path] { return path.read(); }, FindLastOf(path.read(), {L'.'})) +
      LazyString{L"."} + extension);
}

ValueOrError<PathComponent> PathComponent::remove_extension() const {
  return VisitOptional(
      [this](ColumnNumber index) {
        return PathComponent::New(
            read().Substring(ColumnNumber{}, index.ToDelta()));
      },
      [this] { return Success(*this); }, FindLastOf(read(), {L'.'}));
}

std::optional<LazyString> PathComponent::extension() const {
  return VisitOptional(
      [this](ColumnNumber index) -> std::optional<LazyString> {
        return read().Substring(index + ColumnNumberDelta{1});
      },
      [] { return std::nullopt; }, FindLastOf(read(), {L'.'}));
}

Path::Path(PathComponent path_component) : Path(path_component.read()) {}

/* static */ Path Path::Join(Path a, Path b) {
  if (b.GetRootType() == Path::RootType::kAbsolute ||
      a == ValueOrDie(Path::New(LazyString{L"."})) ||
      a == ValueOrDie(Path::New(LazyString{L"./"})))
    return b;
  bool a_ends_in_slash = a.read().get(ColumnNumber{} + a.read().size() -
                                      ColumnNumberDelta(1)) == L'/';
  return ValueOrDie(
      Path::New(a.read() +
                (a_ends_in_slash ? LazyString{L""} : LazyString{L"/"}) +
                b.read()),
      L"Path::Join");
}

Path Path::ExpandHomeDirectory(const Path& home_directory, const Path& path) {
  // TODO: Also support ~user/foo.
  if (!StartsWith(ToLazyString(path), LazyString{L"~"})) return path;
  return std::visit(
      overload{[&](Error) { return path; },
               [&](std::list<PathComponent> components) {
                 if (components.empty() ||
                     components.front() != PathComponent::FromString(L"~"))
                   return path;
                 components.pop_front();
                 return container::Fold(
                     [](PathComponent c, Path output) {
                       return Path::Join(std::move(output), std::move(c));
                     },
                     home_directory, std::move(components));
               }},
      path.DirectorySplit());
}

/* static */ Path Path::WithExtension(const Path& path,
                                      const LazyString& extension) {
  return Path::Join(
      ValueOrDie(path.Dirname(), L"Path::WithExtension"),
      PathComponent::WithExtension(
          ValueOrDie(path.Basename(), L"Path::WithExtension"), extension));
}

ValueOrError<Path> Path::Dirname() const {
  VLOG(5) << "Dirname: " << read();
  std::unique_ptr<char, decltype(&std::free)> tmp(strdup(ToBytes().c_str()),
                                                  &std::free);
  CHECK(tmp != nullptr);
  return Path::New(LazyString{FromByteString(dirname(tmp.get()))});
}

ValueOrError<PathComponent> Path::Basename() const {
  VLOG(5) << "Pathname: " << read();
  std::unique_ptr<char, decltype(&std::free)> tmp(strdup(ToBytes().c_str()),
                                                  &std::free);
  CHECK(tmp != nullptr);
  return PathComponent::New(LazyString{FromByteString(basename(tmp.get()))});
}

std::optional<LazyString> Path::extension() const {
  return std::visit(
      overload{[](Error) { return std::optional<LazyString>(); },
               [](PathComponent component) {
                 return std::optional<LazyString>(component.extension());
               }},
      Basename());
}

ValueOrError<std::list<PathComponent>> Path::DirectorySplit() const {
  std::list<PathComponent> output;
  Path path = *this;
  while (!path.IsRoot() && path != Path::LocalDirectory()) {
    ASSIGN_OR_RETURN(PathComponent base, path.Basename());
    using ::operator<<;
    VLOG(5) << "DirectorySplit: PushFront: " << base;
    output.push_front(base);
    if (output.front().read() == path.read()) return Success(output);
    ASSIGN_OR_RETURN(
        auto dir, AugmentError(LazyString{L"Dirname error"}, path.Dirname()));
    if (dir.read().size() >= path.read().size()) {
      LOG(INFO) << "Unable to advance: " << path << " -> " << dir;
      return Error{LazyString{L"Unable to advance: "} + path.read()};
    }
    VLOG(5) << "DirectorySplit: Advance: " << dir;
    path = std::move(dir);
  }
  return Success(output);
}

bool Path::IsRoot() const { return read() == LazyString{L"/"}; }

Path::RootType Path::GetRootType() const {
  return read().get(ColumnNumber{}) == L'/' ? Path::RootType::kAbsolute
                                            : Path::RootType::kRelative;
}

ValueOrError<AbsolutePath> Path::Resolve() const {
  char* result = realpath(ToBytes().c_str(), nullptr);
  return result == nullptr
             ? Error{LazyString{FromByteString(strerror(errno))}}
             : AbsolutePath::FromString(LazyString{FromByteString(result)});
}

PossibleError PathValidator::Validate(const LazyString& path) {
  if (path.empty()) return Error{LazyString{L"Path can not be empty."}};
  return Success();
}

Path Path::LocalDirectory() {
  return ValueOrDie(Path::New(LazyString{L"."}), L"Path::LocalDirectory");
}
Path Path::Root() {
  return ValueOrDie(Path::New(LazyString{L"/"}), L"Path::Root");
}

ValueOrError<AbsolutePath> AbsolutePath::FromString(LazyString path) {
  if (path.empty()) return Error{LazyString{L"Path can't be empty"}};
  if (path.get(ColumnNumber{}) != L'/')
    return Error{LazyString{L"Absolute path must start with /"}};
  return Success(AbsolutePath(std::move(path)));
}

AbsolutePath::AbsolutePath(LazyString path) : Path(std::move(path)) {}

std::ostream& operator<<(std::ostream& os, const Path& p) {
  os << p.read();
  return os;
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  return Path::Join(ValueOrDie(Path::New(LazyString{a}), L"PathJoin"),
                    ValueOrDie(Path::New(LazyString{b}), L"PathJoin"))
      .read()
      .ToString();
}

ValueOrError<NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>>> OpenDir(
    Path path) {
  VLOG(10) << "Open dir: " << path;
  std::unique_ptr<DIR, std::function<void(DIR*)>> output(
      opendir(path.ToBytes().c_str()), closedir);
  if (output == nullptr)
    return Error(path.read() + LazyString{L": Unable to open directory: "} +
                 LazyString{FromByteString(strerror(errno))});
  return NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>>::Unsafe(
      std::move(output));
}

Path GetHomeDirectory() {
  if (char* env = getenv("HOME"); env != nullptr) {
    return std::visit(overload{[&](Error error) {
                                 LOG(FATAL) << "Invalid home directory (from "
                                               "`HOME` environment variable): "
                                            << error << ": " << env;
                                 return Path::Root();
                               },
                               [](Path path) { return path; }},
                      Path::New(LazyString{FromByteString(env)}));
  }
  if (struct passwd* entry = getpwuid(getuid()); entry != nullptr) {
    return std::visit(
        overload{[&](Error error) {
                   LOG(FATAL)
                       << "Invalid home directory (from `getpwuid`): " << error;
                   return Path::Root();
                 },
                 [](Path path) { return path; }},
        Path::New(LazyString{FromByteString(entry->pw_dir)}));
  }
  return Path::Root();  // What else?
}
}  // namespace afc::infrastructure
