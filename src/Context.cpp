#include "Context.hpp"

#include "args.hpp"
#include "counters.hpp"

Context::~Context()
{
  free(current_working_dir);
  free(stats_file);
  args_free(orig_args);

  free(result_name);
  free(result_path);

  free(manifest_path);

  free(i_tmpfile);
  free(cpp_stderr);
  free(manifest_stats_file);
  free(included_pch_file);

  for (size_t i = 0; i < ignore_headers_len; i++) {
    free(ignore_headers[i]);
  }
  free(ignore_headers);

  counters_free(counter_updates);
}
