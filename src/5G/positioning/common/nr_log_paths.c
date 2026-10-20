#include "nr_log_paths.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define NR_MKDIR(p) _mkdir(p)
#else
#define NR_MKDIR(p) mkdir(p, 0777)
#endif

static void normalize(char *path)
{
  for (; *path; ++path) if (*path == '\\') *path = '/';
}

static int make_directories(char *path)
{
  struct stat st;
  size_t i, n = strlen(path);
  for (i = 1; i <= n; ++i) {
    char saved;
    if (i != n && path[i] != '/') continue;
    if (i == 2 && path[1] == ':') continue;
    saved = path[i]; path[i] = '\0';
    if (NR_MKDIR(path) != 0 &&
        (errno != EEXIST || stat(path, &st) != 0 || !(st.st_mode & S_IFDIR))) {
      path[i] = saved; return -1;
    }
    path[i] = saved;
  }
  return 0;
}

static int resolve(char *path, size_t capacity, const char *requested, const char *fallback, int directory)
{
  char root[4096], name[4096];
  const char *value = requested && *requested ? requested : fallback;
  size_t root_len, n;
  char *part;
  if (!path || !capacity || !value || !*value) return -1;
  if (strlen(NR_DEFAULT_LOG_DIR) >= sizeof(root) || strlen(value) >= sizeof(name)) return -1;
  strcpy(root, NR_DEFAULT_LOG_DIR); strcpy(name, value);
  normalize(root); normalize(name);
  root_len = strlen(root);
  while (root_len > 1 && root[root_len - 1] == '/') root[--root_len] = '\0';
  part = name;
  if (!strncmp(name, root, root_len) && name[root_len] == '/') {
    part = name + root_len + 1;
  } else if (name[0] == '/' || (isalpha((unsigned char)name[0]) && name[1] == ':')) goto invalid;
  if (!*part) goto invalid;
  {
    const char *p = part;
    while (*p) {
      const char *end = strchr(p, '/');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      if (!len || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) goto invalid;
      p += len; if (*p) ++p;
    }
  }
  n = root_len + 1 + strlen(part);
  if (n >= capacity) return -1;
  memcpy(path, root, root_len); path[root_len] = '/'; strcpy(path + root_len + 1, part);
  if (directory) return make_directories(path);
  {
    char *slash = strrchr(path, '/');
    int rc;
    *slash = '\0'; rc = make_directories(path); *slash = '/';
    return rc;
  }
invalid:
  fprintf(stderr, "Log output must stay inside %s (no '..' components): %s\n", root, value);
  return -1;
}

int nr_log_file_path(char *path, size_t capacity, const char *requested, const char *fallback)
{
  return resolve(path, capacity, requested, fallback, 0);
}

int nr_log_directory(char *path, size_t capacity, const char *requested, const char *fallback)
{
  return resolve(path, capacity, requested, fallback, 1);
}
