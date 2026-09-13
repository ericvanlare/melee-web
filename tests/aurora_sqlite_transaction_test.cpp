#include "sqlite_utils.hpp"

#include <cassert>
#include <cstring>

namespace {

int deny_commit(void*, int action, const char* first, const char*, const char*, const char*) {
  return action == SQLITE_TRANSACTION && first != nullptr && std::strcmp(first, "COMMIT") == 0 ? SQLITE_DENY
                                                                                              : SQLITE_OK;
}

int scalar(sqlite3* db, const char* sql) {
  sqlite3_stmt* statement = nullptr;
  assert(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
  const int result = sqlite3_step(statement);
  assert(result == SQLITE_ROW);
  const int value = sqlite3_column_int(statement, 0);
  assert(sqlite3_finalize(statement) == SQLITE_OK);
  return value;
}

void exec(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  assert(result == SQLITE_OK);
  sqlite3_free(error);
}

} // namespace

int main() {
  sqlite3* db = nullptr;
  assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
  aurora::Module log{"sqlite-transaction-test"};
  exec(db, "CREATE TABLE values_table(value INTEGER NOT NULL)");

  {
    aurora::sqlite::Transaction transaction(db, log, true);
    assert(transaction);
    exec(db, "INSERT INTO values_table VALUES (1)");
    assert(transaction.commit());
    assert(!transaction.commit());
  }
  assert(scalar(db, "SELECT COUNT(*) FROM values_table") == 1);

  // A nested BEGIN fails and leaves the transaction inactive; commit must not
  // report success after that failure.
  exec(db, "BEGIN EXCLUSIVE");
  aurora::sqlite::Transaction failed_begin(db, log, true);
  assert(!failed_begin);
  assert(!failed_begin.commit());
  exec(db, "ROLLBACK");

  sqlite3_set_authorizer(db, deny_commit, nullptr);
  {
    aurora::sqlite::Transaction denied_commit(db, log, true);
    assert(denied_commit);
    exec(db, "INSERT INTO values_table VALUES (2)");
    assert(!denied_commit.commit());
  }
  sqlite3_set_authorizer(db, nullptr, nullptr);
  assert(scalar(db, "SELECT COUNT(*) FROM values_table") == 1);

  {
    aurora::sqlite::Transaction rollback_on_destroy(db, log, true);
    assert(rollback_on_destroy);
    exec(db, "INSERT INTO values_table VALUES (3)");
  }
  assert(scalar(db, "SELECT COUNT(*) FROM values_table") == 1);

  assert(sqlite3_close(db) == SQLITE_OK);
}
