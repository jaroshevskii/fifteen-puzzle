module;

// The SQLite C header belongs in the global module fragment. It is a plain C
// header (no libc++ entities), so it coexists with `import std;` — unlike a C++
// header, which would have to live in an implementation unit (see the network
// client). It is an implementation detail and not exported to importers.
#include <sqlite3.h>

export module Sqlite;

import std;

// A thin C++ wrapper over the SQLite C API, mirroring isowords' `Sqlite` type.
// `Database::run` prepares a statement, binds the supplied values, steps
// through the result rows, and returns them as a vector of variant cells.
// Errors throw `Sqlite::Error`; the database client above catches them and maps
// to a typed `std::expected`.
export namespace Sqlite {

// A SQLite cell value: NULL, integer, real, text, or blob.
using Datatype = std::variant<std::monostate, std::int64_t, double, std::string,
                              std::vector<std::uint8_t>>;
using Row = std::vector<Datatype>;

struct Error {
  std::optional<int> code;
  std::string description;
};

class Database {
public:
  explicit Database(const std::string &path) {
    if (sqlite3_open_v2(path.c_str(), &handle_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      Error error{
          handle_ ? std::optional<int>(sqlite3_errcode(handle_)) : std::nullopt,
          handle_ ? sqlite3_errmsg(handle_) : "could not open database"};
      sqlite3_close_v2(handle_);
      handle_ = nullptr;
      throw error;
    }
  }

  ~Database() { sqlite3_close_v2(handle_); }

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  Database(Database &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  Database &operator=(Database &&other) noexcept {
    if (this != &other) {
      sqlite3_close_v2(handle_);
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  // Runs a statement with no result (DDL, PRAGMA writes, etc.).
  void execute(const std::string &sql) {
    char *message = nullptr;
    if (sqlite3_exec(handle_, sql.c_str(), nullptr, nullptr, &message) !=
        SQLITE_OK) {
      Error error{sqlite3_errcode(handle_),
                  message ? message : "statement failed"};
      sqlite3_free(message);
      throw error;
    }
  }

  // Prepares `sql`, binds `bindings` positionally (1-based), and returns all
  // result rows.
  std::vector<Row> run(const std::string &sql,
                       std::vector<Datatype> bindings = {}) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle_, sql.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
      throw Error{sqlite3_errcode(handle_), sqlite3_errmsg(handle_)};
    }

    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const int column = static_cast<int>(i) + 1;
      std::visit(
          [&](auto &&cell) {
            using Cell = std::decay_t<decltype(cell)>;
            if constexpr (std::is_same_v<Cell, std::monostate>) {
              sqlite3_bind_null(stmt, column);
            } else if constexpr (std::is_same_v<Cell, std::int64_t>) {
              sqlite3_bind_int64(stmt, column, cell);
            } else if constexpr (std::is_same_v<Cell, double>) {
              sqlite3_bind_double(stmt, column, cell);
            } else if constexpr (std::is_same_v<Cell, std::string>) {
              sqlite3_bind_text(stmt, column, cell.c_str(),
                                static_cast<int>(cell.size()),
                                SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<Cell,
                                                std::vector<std::uint8_t>>) {
              sqlite3_bind_blob(stmt, column, cell.data(),
                                static_cast<int>(cell.size()),
                                SQLITE_TRANSIENT);
            }
          },
          bindings[i]);
    }

    std::vector<Row> rows;
    int rc = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const int columns = sqlite3_column_count(stmt);
      Row row;
      row.reserve(static_cast<std::size_t>(columns));
      for (int c = 0; c < columns; ++c) {
        switch (sqlite3_column_type(stmt, c)) {
        case SQLITE_INTEGER:
          row.emplace_back(
              static_cast<std::int64_t>(sqlite3_column_int64(stmt, c)));
          break;
        case SQLITE_FLOAT:
          row.emplace_back(sqlite3_column_double(stmt, c));
          break;
        case SQLITE_TEXT: {
          const auto *text =
              reinterpret_cast<const char *>(sqlite3_column_text(stmt, c));
          row.emplace_back(std::string(
              text ? text : "",
              static_cast<std::size_t>(sqlite3_column_bytes(stmt, c))));
          break;
        }
        case SQLITE_BLOB: {
          const auto *bytes =
              static_cast<const std::uint8_t *>(sqlite3_column_blob(stmt, c));
          const int size = sqlite3_column_bytes(stmt, c);
          row.emplace_back(std::vector<std::uint8_t>(bytes, bytes + size));
          break;
        }
        default:
          row.emplace_back(std::monostate{});
          break;
        }
      }
      rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
      throw Error{sqlite3_errcode(handle_), sqlite3_errmsg(handle_)};
    }
    return rows;
  }

  std::int64_t lastInsertRowid() const {
    return sqlite3_last_insert_rowid(handle_);
  }

private:
  sqlite3 *handle_ = nullptr;
};

} // namespace Sqlite
