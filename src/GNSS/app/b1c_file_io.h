#pragma once
#include "bds5g/b1c.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#endif

typedef struct {
  FILE *file;
  unsigned char *raw;
  size_t capacity, width;
} b1c_file_reader_t;

static int b1c_file_open(b1c_file_reader_t *r, const char *path, int ci8)
{
  memset(r,0,sizeof(*r)); r->width=ci8 ? 2U : 8U;
  r->file=fopen(path,"rb"); return r->file ? 0 : -1;
}
static void b1c_file_close(b1c_file_reader_t *r)
{
  if (r->file) fclose(r->file);
  free(r->raw); memset(r,0,sizeof(*r));
}
static int b1c_file_seek(b1c_file_reader_t *r, uint64_t sample)
{
  if (sample>(uint64_t)INT64_MAX/r->width) return -1;
#ifdef _WIN32
  return _fseeki64(r->file,(__int64)(sample*r->width),SEEK_SET);
#else
  return fseeko(r->file,(off_t)(sample*r->width),SEEK_SET);
#endif
}
static int b1c_file_read(b1c_file_reader_t *r, bds5g_cf32_t *out, size_t count, size_t *read_count)
{
  if (count>SIZE_MAX/r->width) return -1;
  if (count*r->width>r->capacity) {
    void *raw=realloc(r->raw,count*r->width); if (!raw) return -1;
    r->raw=(unsigned char *)raw; r->capacity=count*r->width;
  }
  size_t bytes=fread(r->raw,1,count*r->width,r->file);
  if (ferror(r->file) || bytes%r->width) return -1;
  *read_count=bytes/r->width;
  for (size_t k=0;k<*read_count;++k) {
    if (r->width==2) {
      out[k].i=r->raw[2*k]<128 ? r->raw[2*k] : (int)r->raw[2*k]-256;
      out[k].q=r->raw[2*k+1]<128 ? r->raw[2*k+1] : (int)r->raw[2*k+1]-256;
    } else {
      for (int j=0;j<2;++j) {
        unsigned char *b=r->raw+8*k+4*j;
        uint32_t bits=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
        float value; memcpy(&value,&bits,4);
        if (j==0) out[k].i=value; else out[k].q=value;
      }
    }
  }
  return 0;
}
static int b1c_number(const char *s, double *value)
{
  char *end; errno=0; *value=strtod(s,&end);
  return !*s || *end || errno || !isfinite(*value) ? -1 : 0;
}
static int b1c_mkdirs(const char *path)
{
  char buffer[4096]; size_t length=strlen(path);
  if (!length || length>=sizeof(buffer)) return -1;
  memcpy(buffer,path,length+1);
  for (size_t k=1;k<=length;++k) {
    if (buffer[k]!='/' && buffer[k]!='\\' && buffer[k]!='\0') continue;
    char saved=buffer[k]; buffer[k]='\0';
    if (!(k==2 && buffer[1]==':')) {
#ifdef _WIN32
      int status=_mkdir(buffer);
#else
      int status=mkdir(buffer,0777);
#endif
      if (status && errno!=EEXIST) return -1;
      struct stat st; if (stat(buffer,&st) || !(st.st_mode&S_IFDIR)) return -1;
    }
    buffer[k]=saved;
  }
  return 0;
}
static int b1c_same_file(const char *a, const char *b)
{
  struct stat x,y;
  return !strcmp(a,b) || (!stat(a,&x) && !stat(b,&y) && x.st_ino &&
                          x.st_dev==y.st_dev && x.st_ino==y.st_ino);
}
