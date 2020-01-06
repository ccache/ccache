#include "compilation_database.hpp"

#include "Context.hpp"
#include "File.hpp"
#include "Logging.hpp"

#include <third_party/llvm_yaml_escape.hpp>

#include <string.h>

#include <vector>

using Logging::log;

namespace {

bool
needs_to_be_escaped(std::string_view str)
{
  for (auto i = str.begin(), e = str.end(); i != e; ++i) {
    uint8_t v = static_cast<uint8_t>(*i);
    if (v < 0x20 || v > 0x7E || *i == '\\' || *i == '"') {
      return true;
    }
  }
  return false;
}

void
append_quoted_and_escaped(std::string& out, std::string_view sv)
{
  out.append("\"");
  if (needs_to_be_escaped(sv)) {
    out.append(llvm::yaml::escape(sv));
  } else {
    out.append(sv.data(), sv.length());
  }
  out.append("\"");
}

enum Action : int {
  unknown = 0,
  skip,
  skip_arg_also,
  keep,
  keep_and_split_at,
};

struct ArgStatus
{
  static ArgStatus
  unknown()
  {
    return {Action::unknown, 0};
  }

  static ArgStatus
  keep()
  {
    return {Action::keep, 0};
  }

  static ArgStatus
  skip()
  {
    return {Action::skip, 0};
  }

  static ArgStatus
  skip_arg_also()
  {
    return {Action::skip_arg_also, 0};
  }

  static ArgStatus
  keep_and_split_at(size_t pos)
  {
    return {Action::keep_and_split_at, int(pos)};
  }

  Action action;
  int split_pos;
};

// Code which generates the compilation database in clang:
// lib/Driver/ToolChains/Clang.cpp  Clang::DumpCompilationDatabase

// Clang removes "-x" and "-M" arguments from the output and splits various
// arguments, e.g. "-I/path" is always converted to ["-I", "/path"].
//
// See include/clang/Driver/CC1Options.td and include/clang/Driver/Options.td
// for the group definitions, and see the "JoinedOrSeparate" for the splitting:
//
// relevant are: CC1Option Preprocessor_Group clang_i_Group I_Group M_Group
//
// cat include/clang/Driver/Options.td |
//   grep -E 'JoinedOrSeparate.*(CC1Option|(Preprocessor|M|clang_i|I)_Group)'
//
// This function duplicates this behavior.
ArgStatus
inspect_argument(std::string_view arg)
{
  // need at least "-M" to be removed or "-DX" to be split and start with a "-"
  if (arg.length() < 2 || arg.at(0) != '-') {
    return ArgStatus::keep();
  }

  char second = arg.at(1);

  auto keep_and_maybe_split = [](std::string_view arg1,
                                 const char* arg2) -> ArgStatus {
    if (arg1 == arg2) {
      return ArgStatus::keep();
    } else {
      return ArgStatus::keep_and_split_at(std::string_view(arg2).length());
    }
  };

  // single character options
  if (second == 'D' || second == 'F' || second == 'I' || second == 'U'
      || second == 'o') {
    if (arg.length() > 2) {
      return ArgStatus::keep_and_split_at(2);
    } else {
      return ArgStatus::keep();
    }
  } else if (second == 'x') { // ["-x", "c++"] or ["-xc++"]
    if (arg.length() == 2) {
      return ArgStatus::skip_arg_also();
    } else {
      return ArgStatus::skip();
    }
  }
  // -M options: skip all of them
  else if (second == 'M') {
    if (arg.length() == 2) {
      return ArgStatus::skip();
    }
    char third = arg.at(2);
    switch (third) {
    case 'D': // -MD
    case 'G': // -MG
    case 'P': // -MP
    case 'V': // -MV
      return ArgStatus::skip();

    case 'M': { // -MM or // -MMD
      if (arg == "-MM" || arg == "-MMD") {
        return ArgStatus::skip();
      } else {
        // unknown -MM?
        return ArgStatus::unknown();
      }
    }

    // these can be provided as ["-MF", "arg"] or ["-MFarg"], skip both
    case 'F':
    case 'J':
    case 'Q':
    case 'T':
      return (arg.length() == 3) ? ArgStatus::skip_arg_also()
                                 : ArgStatus::skip();

    default:
      // lowercase next, e.g. -Mach
      if (third >= 'a' && third <= 'z') {
        return ArgStatus::keep();
      }
      // other unknown -M? arg
      return ArgStatus::unknown();
    }
  }
  // NOT clang but ccache specific
  else if (second == '-' && util::starts_with(arg, "--ccache-")) {
    return ArgStatus::skip();
  }
  // remaining arguments
  else if (second == 'i') {
    const char* i_args[] = {
      "-interface-stub-version=",
      "-idirafter",
      "-iframework",
      "-imacros",
      "--imacros",
      "-include",
      "--include",
      "-iprefix",
      "-iquote",
      "-isysroot",
      "-isystem",
      "-iwithprefixbefore",
      "-iwithprefix",
      "-iwithsysroot",
      "-ivfsoverlay",
    };
    for (const char* i_arg : i_args) {
      if (util::starts_with(arg, i_arg)) {
        return keep_and_maybe_split(arg, i_arg);
      }
    }
  } else {
    for (const char* misc_arg : {"-cxx-isystem", "-working-directory"}) {
      if (second == misc_arg[1] && util::starts_with(arg, misc_arg)) {
        return keep_and_maybe_split(arg, misc_arg);
      }
    }
  }

  return ArgStatus::keep();
}

std::vector<std::string>
keep_split_remove_arguments(const Args& args)
{
  std::vector<std::string> clangified;

  size_t compiler_position = 0; // set to pos 0 in find_compiler()
  for (size_t i = compiler_position; i < args.size(); ++i) {
    std::string_view arg = args[i];
    ArgStatus status = inspect_argument(arg);
    Action a = status.action;
    switch (a) {
    case Action::unknown:
      LOG("Unknown compiler argument \"{}\" when generating {} file",
          arg,
          CDB_JSON);
      clangified.emplace_back(std::move(arg));
      continue;
    case Action::skip_arg_also:
      i += 1;
      continue;
    case Action::skip:
      continue;
    case Action::keep_and_split_at:
      clangified.emplace_back(arg.substr(0, status.split_pos));
      clangified.emplace_back(arg.substr(status.split_pos));
      continue;
    case Action::keep:
      clangified.emplace_back(std::move(arg));
      continue;
    }
  }

  return clangified;
}

} // anonymous namespace

std::string
generate_cdb_json_data(const Context& ctx)
{
  std::string result = "{ \"directory\": ";

  append_quoted_and_escaped(result, ctx.apparent_cwd);

  result.append(", \"file\": ");
  append_quoted_and_escaped(result, ctx.args_info.input_file);

  result.append(", \"output\": ");
  append_quoted_and_escaped(result, ctx.args_info.output_obj);

  result.append(", \"arguments\": [");

  std::vector<std::string> command = keep_split_remove_arguments(ctx.orig_args);

  const char* const comma_space = ", ";
  for (auto it = command.begin(); it != command.end();) {
    append_quoted_and_escaped(result, *it);
    ++it;
    if (it != command.end()) {
      result.append(comma_space);
    }
  }

  result.append("]},\n");

  return result;
}

void
write_cdb_json(const Context& ctx, const std::string& path)
{
  auto cdb_data = generate_cdb_json_data(ctx);

  File cdb_json_file(std::string(path), "wb");

  if (fwrite(cdb_data.data(), cdb_data.size(), 1, cdb_json_file.get()) != 1) {
    LOG("Failed to write to \"{}\"", std::string(path));
  } else {
    LOG("Generated {}", path);
  }
}
