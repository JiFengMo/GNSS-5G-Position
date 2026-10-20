#include "nr_log_paths.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "log path test failed at %d\n", __LINE__); return 1; } } while (0)
int main(void)
{
  char path[4096], again[4096], tiny[2];
  FILE *fp;
  struct stat st;
  CHECK(!nr_log_file_path(path, sizeof(path), "tests/log_paths/nested/output.log", NULL));
  fp = fopen(path, "w"); CHECK(fp != NULL);
  CHECK(fputs("log path test\n", fp) >= 0); CHECK(!fclose(fp));
  CHECK(!nr_log_file_path(again, sizeof(again), path, NULL));
  CHECK(!strcmp(path, again));
  CHECK(nr_log_directory(tiny, sizeof(tiny), NULL, "iq") != 0);
  CHECK(nr_log_file_path(again, sizeof(again), "../outside.log", NULL) != 0);
  CHECK(nr_log_file_path(again, sizeof(again), "a/../../outside.log", NULL) != 0);
  CHECK(nr_log_file_path(again, sizeof(again), "/tmp/outside.log", NULL) != 0);
  CHECK(!nr_log_directory(again, sizeof(again), NULL, "tests/log_paths/iq"));
  CHECK(!stat(again, &st) && (st.st_mode & S_IFDIR));
  CHECK(!remove(path));
  puts("central log paths passed");
  return 0;
}
