#include "nr_config_router.h"
#include "nr_pos_api.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text)
{
  char *end;
  while (isspace((unsigned char)*text)) ++text;
  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) --end;
  *end = '\0';
  return text;
}
static int store_path(char *dst, size_t size, const char *src)
{
  if (strlen(src) >= size) return -1;
  memcpy(dst, src, strlen(src) + 1U);
  return 0;
}
static int relative_to(const char *owner, const char *value, char *result, size_t size)
{
  const char *a = strrchr(owner, '/'), *b = strrchr(owner, '\\'), *slash = a;
  size_t dir_len;
  if (b && (!slash || b > slash)) slash = b;
  if (value[0] == '/' || value[0] == '\\' || (isalpha((unsigned char)value[0]) && value[1] == ':'))
    return store_path(result, size, value);
  dir_len = slash ? (size_t)(slash - owner + 1) : 0U;
  if (dir_len + strlen(value) >= size) return -1;
  memcpy(result, owner, dir_len); strcpy(result + dir_len, value);
  return 0;
}
int nr_toa_load_main_config(const char *main_path, const char *role, nr_toa_app_cfg_t *cfg)
{
  FILE *fp;
  char line[4096], receiver[2048] = "", anchor[2048] = "";
  char monitor_path[2048] = "";
  char child_path[4096], resolved[4096];
  unsigned lineno = 0U, seen = 0U;
  int enabled = -1, result = -1;
  if (!main_path || !role || !cfg || (strcmp(role, "receiver") && strcmp(role, "anchor"))) return -1;
  fp = fopen(main_path, "r");
  if (!fp) { fprintf(stderr, "Cannot open main config: %s\n", main_path); return -1; }
  while (fgets(line, sizeof(line), fp)) {
    char *key, *value, *eq;
    unsigned bit = 0U;
    ++lineno; key = line;
    if (lineno == 1U && (unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf) key += 3;
    if (strlen(line) == sizeof(line) - 1U && !strchr(line, '\n')) goto invalid;
    key = trim(key);
    if (!*key || *key == '#') continue;
    eq = strchr(key, '='); if (!eq) goto invalid;
    *eq = '\0'; value = trim(eq + 1); key = trim(key);
    if (!*value) goto invalid;
    if (!strcmp(key, "enable_5g")) {
      bit = 1U;
      if (strcmp(value, "0") && strcmp(value, "1")) goto invalid;
      enabled = value[0] == '1';
    } else if (!strcmp(key, "nr_config")) {
      bit = 2U; if (store_path(receiver, sizeof(receiver), value)) goto invalid;
    } else if (!strcmp(key, "nr_anchor_config")) {
      bit = 4U; if (store_path(anchor, sizeof(anchor), value)) goto invalid;
    } else if (!strcmp(key, "ui_config")) {
      bit = 128U; if (store_path(monitor_path, sizeof(monitor_path), value)) goto invalid;
    } else if (!strcmp(key, "enable_gnss") || !strcmp(key, "enable_fusion")) {
      bit = !strcmp(key, "enable_gnss") ? 8U : 16U;
      if (strcmp(value, "0") && strcmp(value, "1")) goto invalid;
      if (!strcmp(value, "1")) fprintf(stderr, "%s is reserved; this executable only runs 5G.\n", key);
    } else if (!strcmp(key, "gnss_config") || !strcmp(key, "fusion_config")) {
      bit = !strcmp(key, "gnss_config") ? 32U : 64U;
    } else goto invalid;
    if (seen & bit) goto invalid;
    seen |= bit;
  }
  if (ferror(fp)) goto done;
  if (enabled != 1) { fprintf(stderr, "5G is disabled or enable_5g is missing in %s\n", main_path); goto done; }
  {
    const char *child = !strcmp(role, "anchor") ? anchor : receiver;
    if (!*child || relative_to(main_path, child, child_path, sizeof(child_path))) goto done;
  }
  if (nr_toa_load_config(child_path, cfg)) { fprintf(stderr, "Cannot load 5G child config: %s\n", child_path); goto done; }
  if (*monitor_path &&
      (relative_to(main_path, monitor_path, resolved, sizeof(resolved)) ||
       store_path(cfg->monitor_config_path, sizeof(cfg->monitor_config_path), resolved))) goto done;
  if (cfg->anchor_db_path[0] &&
      (relative_to(child_path, cfg->anchor_db_path, resolved, sizeof(resolved)) ||
       store_path(cfg->anchor_db_path, sizeof(cfg->anchor_db_path), resolved))) {
    fprintf(stderr, "Resolved anchor path is too long in %s\n", child_path); goto done;
  }
  if (!nr_toa_select_provider(cfg)) {
    fprintf(stderr, "Unsupported software provider: %s\n", cfg->provider); goto done;
  }
  printf("config: main=%s role=%s child=%s\n", main_path, role, child_path);
  result = 0; goto done;
invalid:
  fprintf(stderr, "Invalid or duplicate main-config item at %s:%u\n", main_path, lineno);
done:
  fclose(fp); return result;
}
