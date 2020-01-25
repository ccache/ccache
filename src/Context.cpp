#include "Context.hpp"

Context::~Context()
{
  free(stats_file);
}
