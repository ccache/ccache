unittest_sources = [
  'framework.c',
  'main.c',
  'util.c',
  'test_args.c',
  'test_argument_processing.c',
  'test_compopt.c',
  'test_conf.c',
  'test_counters.c',
  'test_hash.c',
  'test_hashutil.c',
  'test_lockfile.c',
  'test_stats.c',
  'test_util.c',
]

unittest = executable('ccache-unittest',
  unittest_sources,
  c_args : [config_h_flag],
  include_directories: [config_h_inc],
  install : true,
  dependencies : ccache_dep,
)

test('ccache-unittest',
  unittest,
  timeout : 5 * 60)
