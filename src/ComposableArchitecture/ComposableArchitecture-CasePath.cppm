export module ComposableArchitecture:CasePath;

import std;

// A C++ port of the case-path concept underpinning TCA's `@CasePathable`
// action enums. A `CasePath<Whole, Part>` is a pair of functions that extract a
// `Part` out of a `Whole` (when present) and embed a `Part` back into a
// `Whole`.
export namespace ComposableArchitecture {

template <typename Whole, typename Part> struct CasePath {
  std::function<std::optional<Part>(const Whole &)> extract;
  std::function<Whole(Part)> embed;
};

// Builds a case path for a `std::variant` `Whole` whose `Case` alternative
// wraps a `Part` in a single member.
template <typename Whole, typename Case, typename Part>
CasePath<Whole, Part> casePath(Part Case::*member) {
  return CasePath<Whole, Part>{
      [member](const Whole &whole) -> std::optional<Part> {
        if (const auto *wrapped = std::get_if<Case>(&whole)) {
          return wrapped->*member;
        }
        return std::nullopt;
      },
      [member](Part part) -> Whole {
        Case wrapped{};
        wrapped.*member = std::move(part);
        return Whole{std::move(wrapped)};
      }};
}

} // namespace ComposableArchitecture
