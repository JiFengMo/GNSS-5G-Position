#pragma once
/* Windows CRT has a one-argument mkdir; leave DSP source unchanged. */
#ifdef _WIN32
#include <sys/stat.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif
