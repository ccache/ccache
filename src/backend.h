#ifndef BACKEND_ADAPTER_H
#define BACKEND_ADAPTER_H

#include "stddef.h"

typedef struct backend_load {
  char* data_obj;
  char* data_stderr;
  char* data_dia;
  char* data_dep;
  size_t size_obj;
  size_t size_stderr;
  size_t size_dia;
  size_t size_dep;
} backend_load;

typedef struct backend {
  void (*init)(void* configuration);
  void (*done)(void);
  int (*from_cache)(const char* id, backend_load *load);
  int (*from_cache_string)(const char* id, char** string, size_t* size);
  int (*to_cache)(const char* id, backend_load* load);
  int (*to_cache_string)(const char* id, char* string, size_t size);
} backend;

void create_backend(const char* name, backend* backend);

#endif
