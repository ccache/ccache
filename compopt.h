#ifndef CCACHE_COMPOPT_H
#define CCACHE_COMPOPT_H

#include "system.h"

bool compopt_short(bool (*fn)(const char *option), const char *option);
bool compopt_affects_cpp(const char *option);
bool compopt_too_hard(const char *option);
bool compopt_too_hard_for_direct_mode(const char *option);
bool compopt_takes_path(const char *option);
bool compopt_takes_arg(const char *option);
bool compopt_prefix_affects_cpp(const char *option);

#endif /* CCACHE_COMPOPT_H */
