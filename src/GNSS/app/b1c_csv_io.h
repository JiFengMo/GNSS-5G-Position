#pragma once
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define B1C_MKDIR(p) _mkdir(p)
#else
#define B1C_MKDIR(p) mkdir(p,0777)
#endif
typedef struct { FILE *file; char line[16384],*columns[128]; int count; } b1c_csv_t;
static inline int csv_split(b1c_csv_t *c)
{
  size_t n=strcspn(c->line,"\r\n");
  if (n>=sizeof(c->line)-1) return -1;
  c->line[n]=0; int count=0; c->columns[count++]=c->line;
  for (size_t j=0;j<n;++j) if (c->line[j]==',') {
    if (count==128) return -1;
    c->line[j]=0; c->columns[count++]=c->line+j+1;
  }
  return count;
}
static inline int csv_open(b1c_csv_t *c,const char *p)
{
  memset(c,0,sizeof(*c)); c->file=fopen(p,"r");
  if (!c->file) return -1;
  if (!fgets(c->line,sizeof(c->line),c->file)||(c->count=csv_split(c))<1) {
    fclose(c->file); c->file=NULL; return -1;
  }
  return 0;
}
static inline int csv_index(const b1c_csv_t *c,const char *name)
{
  int found=-1;
  for (int j=0;j<c->count;++j) if (!strcmp(c->columns[j],name)) {
    if (found>=0) return -1;
    found=j;
  }
  return found;
}
static inline int csv_next(b1c_csv_t *c)
{
  if (!fgets(c->line,sizeof(c->line),c->file)) return ferror(c->file)?-1:0;
  return csv_split(c)==c->count ? 1 : -1;
}
static inline int csv_u64(const char *s,uint64_t *out)
{
  if (*s<'0'||*s>'9') return -1;
  char *end; errno=0; unsigned long long v=strtoull(s,&end,10);
  if (errno||*end) return -1;
  *out=(uint64_t)v; return 0;
}
static inline int csv_number(const char *s,double *out)
{
  char *end; errno=0; *out=strtod(s,&end);
  return !*s||*end||errno||!isfinite(*out) ? -1 : 0;
}
static inline int csv_close(FILE *f)
{ if (!f) return 0; int bad=ferror(f); return fclose(f)!=0||bad; }
static inline int csv_new_directory(const char *path)
{
  char p[4096]; size_t n=strlen(path); struct stat st;
  if (!n||n>=sizeof(p)||!stat(path,&st)||errno!=ENOENT) return -1;
  memcpy(p,path,n+1);
  for (size_t j=1;j<=n;++j) if (!p[j]||p[j]=='/'||p[j]=='\\') {
    char saved=p[j]; p[j]=0;
    if (!(j==2&&p[1]==':')&&B1C_MKDIR(p)&&errno!=EEXIST) return -1;
    p[j]=saved;
  }
  return 0;
}
static inline FILE *csv_output(const char *directory,const char *name)
{
  char p[4096];
  if (snprintf(p,sizeof(p),"%s/%s",directory,name)>=(int)sizeof(p)) return NULL;
  return fopen(p,"wx");
}
static inline void csv_json_string(FILE *f,const char *s)
{
  fputc('"',f);
  for (;*s;++s) {
    unsigned char c=(unsigned char)*s;
    if (c=='"'||c=='\\') { fputc('\\',f); fputc(c,f); }
    else if (c<32) fprintf(f,"\\u%04x",c);
    else fputc(c,f);
  }
  fputc('"',f);
}
