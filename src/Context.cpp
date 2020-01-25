#include "Context.hpp"

#include "args.hpp"

Context::~Context()
{
  free(stats_file);
  args_free(orig_args);

  free(result_name);
  free(result_path);

  free(manifest_path);
}
