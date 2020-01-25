#include "Context.hpp"

#include "args.hpp"

Context::~Context()
{
  free(stats_file);
  args_free(orig_args);
}
