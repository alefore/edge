#ifndef __AFC_EDITOR_FUZZ_INPUT_H__
#define __AFC_EDITOR_FUZZ_INPUT_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>

#include "src/language/wstring.h"
#include "src/tests/fuzz_testable.h"

namespace afc {
namespace editor {
namespace fuzz {

// value will never include '\n'.
struct ShortRandomLine {
  std::wstring value;
};

struct ShortRandomString {
  std::wstring value;
};

template <class T>
struct Reader {};

template <>
struct Reader<size_t> {
  static std::optional<size_t> Read(Stream& input_stream);
};

template <>
struct Reader<ShortRandomLine> {
  static std::optional<ShortRandomLine> Read(Stream& input_stream);
};

template <>
struct Reader<ShortRandomString> {
  static std::optional<ShortRandomString> Read(Stream& input_stream);
};

Handler Call(std::function<void()> callback);

template <typename A, typename B>
Handler Call(std::function<void(A, B)> callback) {
  return [callback](Stream& input_stream) {
    std::optional<A> value_a = Reader<A>::Read(input_stream);
    std::optional<B> value_b = Reader<B>::Read(input_stream);
    if (value_a.has_value() && value_b.has_value()) {
      callback(value_a.value(), value_b.value());
    }
  };
}

template <typename A>
Handler Call(std::function<void(A)> callback) {
  return [callback](Stream& input_stream) {
    std::optional<A> value = Reader<A>::Read(input_stream);
    if (value.has_value()) {
      callback(value.value());
    }
  };
}

}  // namespace fuzz
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FUZZ_INPUT_H__
