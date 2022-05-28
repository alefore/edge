#ifndef __AFC_LANGUAGE_OVERLOAD_H__
#define __AFC_LANGUAGE_OVERLOAD_H__

namespace afc::language {
template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;
}  // namespace afc::language
#endif
