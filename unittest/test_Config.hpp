#pragma once

#include "../src/Config.hpp"

// Helper to modify private Config variables without adding setters that would
// only be used for testing.
// This struct MUST NOT have any data members, only the required private member
// variables should be exposed.
struct ConfigTester : Config {
  using Config::m_cache_dir_levels;
};
