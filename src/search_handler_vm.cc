#include <memory>

#include "src/buffer_vm.h"
#include "src/concurrent/protected.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/overload.h"
#include "src/search_handler.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::vm::Environment;
using afc::vm::Identifier;

using ValueType =
    NonNull<std::shared_ptr<Protected<afc::editor::SearchOptions>>>;

namespace afc::vm {

template <>
const types::ObjectName VMTypeMapper<ValueType>::object_type_name =
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"SearchOptions")}};
}  // namespace afc::vm
namespace afc::editor {
void RegisterSearchOptionsVm(gc::Pool& pool, Environment& environment) {
  using vm::ObjectType;
  using vm::Trampoline;
  using vm::VMTypeMapper;
  using vm::types::ObjectName;

  gc::Root<ObjectType> search_options_type =
      ObjectType::New(pool, VMTypeMapper<ValueType>::object_type_name);

  // Constructor.
  environment.Define(
      std::get<ObjectName>(search_options_type.ptr()->type()).read(),
      vm::NewCallback(pool, vm::kPurityTypePure, [] { return ValueType{}; }));

  // The regular expression to search.
  search_options_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"set_query")},
      vm::NewCallback(pool, vm::kPurityTypeUnknown,
                      [](ValueType search_options,
                         LazyString query) -> ValueOrError<ValueType> {
                        DECLARE_OR_RETURN(SingleLine query_converted,
                                          SingleLine::New(query));
                        search_options->lock(
                            [&query_converted](SearchOptions& data) {
                              data.search_query = query_converted;
                            });
                        return search_options;
                      })
          .ptr());

  search_options_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"search")},
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](ValueType search_options, gc::Ptr<OpenBuffer> buffer) {
            using OutputType =
                NonNull<std::shared_ptr<Protected<std::vector<LineColumn>>>>;
            return search_options->lock(
                [buffer_contents = buffer->contents().snapshot()](
                    const SearchOptions& data) -> ValueOrError<OutputType> {
                  DECLARE_OR_RETURN(std::vector<LineColumn> positions,
                                    SearchHandler(Direction::kForwards, data,
                                                  buffer_contents));
                  return MakeNonNullShared<Protected<std::vector<LineColumn>>>(
                      MakeProtected(std::move(positions)));
                });
          })
          .ptr());

  using VPB = std::vector<gc::Ptr<OpenBuffer>>;
  using PVPB = Protected<VPB>;
  search_options_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"filter")},
      vm::NewCallback(
          pool,
          vm::PurityType{.writes_external_outputs = true,
                         .reads_external_inputs = true},
          [](ValueType search_options, NonNull<std::shared_ptr<PVPB>> input) {
            return search_options->lock(
                [&](const SearchOptions& search_options_data) {
                  return MakeNonNullShared<PVPB>(
                      input->lock([&search_options_data](VPB input_buffers) {
                        return container::MaterializeVector(
                            input_buffers |
                            std::views::filter([&search_options_data](
                                                   const gc::Ptr<OpenBuffer>&
                                                       buffer) {
                              return std::visit(
                                  overload{
                                      [](Error) { return false; },
                                      [&buffer](
                                          std::vector<LineColumn> positions) {
                                        if (positions.empty()) return false;
                                        buffer->set_position(positions.front());
                                        return true;
                                      }},
                                  SearchHandler(Direction::kForwards,
                                                search_options_data,
                                                buffer->contents().snapshot()));
                            }));
                      }));
                });
          })
          .ptr());
  environment.DefineType(search_options_type.ptr());
}
}  // namespace afc::editor
