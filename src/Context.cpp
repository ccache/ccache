#include "Context.hpp"

#include "args.hpp"

Context::~Context()
{
  free(stats_file);
  args_free(orig_args);

  free(result_name);
  free(result_path);

  free(manifest_path);

  free(i_tmpfile);
  free(cpp_stderr);
  free(manifest_stats_file);
  free(included_pch_file);
}
