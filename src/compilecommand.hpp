#pragma once

#include "system.hpp"

#include <third_party/nonstd/string_view.hpp>

class Context;

// this function must not throw as it is called the Context destructor
void write_compile_command_json(const Context& ctx,
                                nonstd::string_view ccmd_file,
                                nonstd::string_view srcfile,
                                nonstd::string_view object);
