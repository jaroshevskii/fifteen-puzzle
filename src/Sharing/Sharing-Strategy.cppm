export module Sharing:Strategy;

import std;

// A C++ port of the persistence half of Point-Free's `Sharing` library. A
// `PersistenceStrategy<T>` is a type-erased pair of closures (load / save) that
// back a `Shared<T>` value with some store. Concrete strategies: `inMemory`
// (here, for tests/previews) and `fileStorage` (JSON, in `SharingLive`).
export namespace Sharing {

template <typename T> struct PersistenceStrategy {
  // Reads the persisted value, or nullopt if there is nothing stored yet (or it
  // could not be read — a corrupt store reads as "absent", so the caller falls
  // back to a default rather than crashing).
  std::function<std::optional<T>()> load = [] { return std::optional<T>{}; };
  // Writes the value. May block (file I/O), so callers run it off the main
  // thread via the store's `addTask`.
  std::function<void(const T &)> save = [](const T &) {};
};

// An in-memory strategy backed by a shared cell. Two `Shared<T>` built from the
// same strategy observe each other's saves — handy for deterministic tests.
template <typename T> PersistenceStrategy<T> inMemory(T initial = {}) {
  auto cell = std::make_shared<std::optional<T>>(std::move(initial));
  return PersistenceStrategy<T>{
      .load = [cell] { return *cell; },
      .save = [cell](const T &value) { *cell = value; }};
}

// A generic file-backed strategy: all the file I/O (read, atomic write via
// temp-file + rename) lives here in pure `std`, while serialization is injected
// as `encode`/`decode` closures. This keeps any concrete serializer (e.g. the
// JSON library, with its heavy C++ headers) out of this module's reachable
// interface — the serializer is supplied from a module implementation unit
// where its headers stay private. `decode` returns nullopt on a parse failure
// (a corrupt file reads as "absent", so callers fall back to a default).
template <typename T>
PersistenceStrategy<T>
fileStorage(std::filesystem::path path,
            std::function<std::string(const T &)> encode,
            std::function<std::optional<T>(std::string_view)> decode) {
  return PersistenceStrategy<T>{
      .load = [path, decode = std::move(decode)]() -> std::optional<T> {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
          return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
          return std::nullopt;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        return decode(text);
      },
      .save =
          [path, encode = std::move(encode)](const T &value) {
            std::error_code ec;
            if (path.has_parent_path()) {
              std::filesystem::create_directories(path.parent_path(), ec);
            }
            const std::string text = encode(value);
            // Write to a temp file then rename, so a crash mid-write can't
            // leave a torn file (the file-storage analog of a transaction).
            const std::filesystem::path tmp =
                std::filesystem::path(path).concat(".tmp");
            {
              std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
              if (!out) {
                return;
              }
              out.write(text.data(), static_cast<std::streamsize>(text.size()));
            }
            std::filesystem::rename(tmp, path, ec);
          }};
}

} // namespace Sharing
