// Minimal version of ValueOrError that ghost_type_class can depend on.
//
// Error is forward declared here and define afterwards based on GhostType.

#ifndef __AFC_EDITOR_LANGUAGE_ERROR_BASE_H__
#define __AFC_EDITOR_LANGUAGE_ERROR_BASE_H__

#include <expected>

namespace afc::language {
class Error;

template <typename T>
using ValueOrError = std::expected<T, Error>;

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#define RETURN_IF_ERROR(expr)                               \
  if (auto CONCAT(return_if_error_result, __LINE__) = expr; \
      !CONCAT(return_if_error_result, __LINE__))            \
  return CONCAT(return_if_error_result, __LINE__).error()

#define DECLARE_OR_RETURN(variable, expr)              \
  decltype(auto) CONCAT(tmp_result_, __LINE__) = expr; \
  if (!CONCAT(tmp_result_, __LINE__))                  \
    return CONCAT(tmp_result_, __LINE__).error();      \
  variable = std::move(CONCAT(tmp_result_, __LINE__)).value();

#define DECLARE_OR_RETURN_OTHER(variable, expr, other) \
  decltype(auto) CONCAT(tmp_result_, __LINE__) = expr; \
  if (!CONCAT(tmp_result_, __LINE__)) return other;    \
  variable = std::move(CONCAT(tmp_result_, __LINE__)).value();

#define ASSIGN_OR_RETURN(variable, expression) \
  variable = ({                                \
    auto tmp = expression;                     \
    if (!tmp) return std::move(tmp).error();   \
    std::move(tmp).value();                    \
  })
}  // namespace afc::language
#endif
