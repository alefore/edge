#include "src/vm/file_system.h"

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/string.h"

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;

namespace afc::vm {
void RegisterFileSystemFunctions(
    language::gc::Pool& pool,
    language::NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    Environment& environment) {
  environment.Define(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"Unlink"}}}},
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [file_system_driver](Path target_path)
                          -> futures::ValueOrError<EmptyValue> {
                        return file_system_driver->Unlink(target_path);
                      }));
  environment.Define(
      Identifier{NonEmptySingleLine{SingleLine{LazyString{L"Glob"}}}},
      vm::NewCallback(
          pool, kPurityTypeReader,
          [file_system_driver](LazyString pattern)
              -> futures::ValueOrError<NonNull<
                  std::shared_ptr<Protected<std::vector<LazyString>>>>> {
            return file_system_driver->Glob(pattern).Transform(
                [](std::vector<Path> input)
                    -> language::ValueOrError<NonNull<
                        std::shared_ptr<Protected<std::vector<LazyString>>>>> {
                  return language::Success(
                      MakeNonNullShared<Protected<std::vector<LazyString>>>(
                          MakeProtected(language::container::MaterializeVector(
                              input | std::views::transform([](Path& path) {
                                return language::lazy_string::ToLazyString(
                                    path);
                              })))));
                });
          }));
}
}  // namespace afc::vm
