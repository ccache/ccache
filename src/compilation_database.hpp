#pragma once

#include "Context.hpp"

#include <string_view>

// Default suffix for the compilation database json fragment.
// Not just .json because of the trailing comma and to
// enable `find`-ing just the cdb files in a build tree.
constexpr std::string_view CDB_JSON{".cdb.json"};

class Context;

inline bool
compilation_database_enabled(const Context& ctx)
{
  return ctx.args_info.output_cdb_json.has_value()
         || ctx.config.generate_compilation_database();
}

std::string generate_cdb_json_data(const Context& ctx);
void write_cdb_json(const Context& ctx, const std::string& path);
