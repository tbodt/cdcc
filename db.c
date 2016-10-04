#include "db.h"

#include <gio/gio.h>

#include <string.h>
#include <stdio.h>

sqlite3 *db_open(const char *path)
{
  sqlite3 *db = NULL;

  int res = sqlite3_open(path, &db);
  if (res != SQLITE_OK) {
    g_warning("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  res = sqlite3_busy_timeout(db, 1000);

  if (res != SQLITE_OK) {
    g_warning("SQL: Could not set busy timeout");
  }

  static const char *sql =
    "CREATE TABLE IF NOT EXISTS "
    "cflags(dir TEXT, file TEXT, flags TEXT, "
    "PRIMARY KEY(dir, file) ON CONFLICT REPLACE);";

  char *emsg = NULL;
  res = sqlite3_exec(db, sql, 0, 0, &emsg);

  if (res != SQLITE_OK) {
    g_warning("SQL error: %s\n", emsg);
    sqlite3_free(emsg);
    sqlite3_close(db);
    return NULL;
  }

  return db;
}

void db_close(sqlite3 *db) {
  if (db != NULL) {
    sqlite3_close(db);
  }
}

void db_insert(sqlite3 *db, GList *files, const gchar * const *argv) {
  static const char *sql =
    "INSERT INTO cflags(dir, file, flags) VALUES(?, ?, ?);";

  sqlite3_stmt *stmt;

  int res = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
  if (res != SQLITE_OK) {
    g_warning("SQL: Could not prepare statement: %s", sqlite3_errmsg(db));
    return;
  }

  g_autofree gchar *cwd = g_get_current_dir();
  g_autofree gchar *flags = g_strjoinv(" ", (gchar **) argv);

  g_autoptr(GFile) dir = g_file_new_for_path(cwd);

  GList *iter;
  for (iter = files; iter; iter = iter ->next) {
    const char *fn = (const char *) iter->data;

    g_autoptr(GFile) f = NULL;
    if (g_path_is_absolute(fn)) {
      f = g_file_new_for_path(fn);
    } else {
      f = g_file_resolve_relative_path(dir, fn);
    }

    char *abspath = g_file_get_path(f);

    res = sqlite3_bind_text(stmt, 1, cwd, strlen(cwd), 0);
    if (res != SQLITE_OK) {
      g_warning("SQL: could not bind for %s\n", fn);
      continue;
    }

    res = sqlite3_bind_text(stmt, 2, abspath, strlen(abspath), 0);

    if (res != SQLITE_OK) {
      g_warning("SQL: could not bind for %s\n", fn);
      g_free(abspath);
      continue;
    }

    res = sqlite3_bind_text(stmt, 3, flags, strlen(flags), 0);
    if (res != SQLITE_OK) {
      g_warning("SQL: could not bind for %s\n", fn);
      g_free(abspath);
      continue;
    }

    //insert data now
    res = sqlite3_step(stmt);

    g_free(abspath);

    if (res != SQLITE_DONE) {
      g_warning("SQL: could not insert for %s\n", fn);
      break;
    } else {
      //should not fail after a successful call to _step()
      sqlite3_reset(stmt);
    }
  }

}

gboolean
db_query (sqlite3 *db, const char *path, db_query_result_fn fn, gpointer user_data)
{
   const char *sql = "SELECT * from cflags WHERE dir GLOB ?";

  sqlite3_stmt *stmt;

  int res = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
  if (res != SQLITE_OK) {
    g_warning("SQL: Could not prepare statement: %s", sqlite3_errmsg(db));
    return FALSE;
  }

  res = sqlite3_bind_text(stmt, 1, path, strlen(path), 0);
  if (res != SQLITE_OK) {
    g_warning("SQL: could not bind for %s\n", path);
    return FALSE;
  }

  for (gboolean keep_going = TRUE; keep_going; ) {
    res = sqlite3_step(stmt);

    if (res == SQLITE_ROW) {
      Record rec;
      rec.dir = sqlite3_column_text(stmt, 0);
      rec.filename = sqlite3_column_text(stmt, 1);
      rec.args = sqlite3_column_text(stmt, 2);

      if (rec.dir == NULL || rec.filename == NULL || rec.args == NULL) {
        fprintf(stderr, "SQL: NULL values in row. skipping");
        continue;
      }

      keep_going = (*fn)(&rec, user_data);

    } else if (res == SQLITE_DONE) {
      break;
    } else {
      fprintf(stderr, "SQL: Could not get data: %s\n", sqlite3_errmsg(db));
      return FALSE;
    }

  }

  sqlite3_finalize(stmt);
  return TRUE;
}
