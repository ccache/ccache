// Copyright (C) 2020 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "argprocessing.hpp"

#include "Context.hpp"
#include "FormatNonstdStringView.hpp"
#include "compopt.hpp"
#include "language.hpp"
#include "logging.hpp"

#include <cassert>

using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

namespace {

struct ArgumentProcessingState
{
  bool found_c_opt = false;
  bool found_dc_opt = false;
  bool found_S_opt = false;
  bool found_pch = false;
  bool found_fpch_preprocess = false;
  bool found_color_diagnostics = false;
  bool found_directives_only = false;
  bool found_rewrite_includes = false;

  std::string explicit_language; // As specified with -x.
  std::string file_language;     // As deduced from file extension.
  std::string input_charset;

  // Is the dependency makefile name overridden with -MF?
  bool dependency_filename_specified = false;

  // Is the dependency makefile target name specified with -MT or -MQ?
  bool dependency_target_specified = false;

  // Is the dependency target name implicitly specified using
  // DEPENDENCIES_OUTPUT or SUNPRO_DEPENDENCIES?
  bool dependency_implicit_target_specified = false;

  // Is the compiler being asked to output debug info on level 3?
  bool generating_debuginfo_level_3 = false;

  // common_args contains all original arguments except:
  // * those that never should be passed to the preprocessor,
  // * those that only should be passed to the preprocessor (if run_second_cpp
  //   is false), and
  // * dependency options (like -MD and friends).
  Args common_args;

  // cpp_args contains arguments that were not added to common_args, i.e. those
  // that should only be passed to the preprocessor if run_second_cpp is false.
  // If run_second_cpp is true, they will be passed to the compiler as well.
  Args cpp_args;

  // dep_args contains dependency options like -MD. They are only passed to the
  // preprocessor, never to the compiler.
  Args dep_args;

  // compiler_only_args contains arguments that should only be passed to the
  // compiler, not the preprocessor.
  Args compiler_only_args;
};

bool
color_output_possible()
{
  const char* term_env = getenv("TERM");
  return isatty(STDERR_FILENO) && term_env && strcasecmp(term_env, "DUMB") != 0;
}

bool
detect_pch(Context& ctx,
           const std::string& option,
           const std::string& arg,
           bool* found_pch)
{
  assert(found_pch);

  // Try to be smart about detecting precompiled headers.
  std::string pch_file;
  if (option == "-include-pch" || option == "-include-pth") {
    if (Stat::stat(arg)) {
      cc_log("Detected use of precompiled header: %s", arg.c_str());
      pch_file = arg;
    }
  } else {
    for (const auto& extension : {".gch", ".pch", ".pth"}) {
      std::string path = arg + extension;
      if (Stat::stat(path)) {
        cc_log("Detected use of precompiled header: %s", path.c_str());
        pch_file = path;
      }
    }
  }

  if (!pch_file.empty()) {
    if (!ctx.included_pch_file.empty()) {
      cc_log("Multiple precompiled headers used: %s and %s",
             ctx.included_pch_file.c_str(),
             pch_file.c_str());
      return false;
    }
    ctx.included_pch_file = pch_file;
    *found_pch = true;
  }
  return true;
}

bool
process_profiling_option(Context& ctx, const std::string& arg)
{
  std::string new_profile_path;
  bool new_profile_use = false;

  if (Util::starts_with(arg, "-fprofile-dir=")) {
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else if (arg == "-fprofile-generate" || arg == "-fprofile-instr-generate") {
    ctx.args_info.profile_generate = true;
    if (ctx.guessed_compiler == GuessedCompiler::clang) {
      new_profile_path = ".";
    } else {
      // GCC uses $PWD/$(basename $obj).
      new_profile_path = ctx.apparent_cwd;
    }
  } else if (Util::starts_with(arg, "-fprofile-generate=")
             || Util::starts_with(arg, "-fprofile-instr-generate=")) {
    ctx.args_info.profile_generate = true;
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else if (arg == "-fprofile-use" || arg == "-fprofile-instr-use"
             || arg == "-fprofile-sample-use" || arg == "-fbranch-probabilities"
             || arg == "-fauto-profile") {
    new_profile_use = true;
    if (ctx.args_info.profile_path.empty()) {
      new_profile_path = ".";
    }
  } else if (Util::starts_with(arg, "-fprofile-use=")
             || Util::starts_with(arg, "-fprofile-instr-use=")
             || Util::starts_with(arg, "-fprofile-sample-use=")
             || Util::starts_with(arg, "-fauto-profile=")) {
    new_profile_use = true;
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else if (arg == "-fprofile-sample-accurate") {
    return true;
  } else {
    cc_log("Unknown profiling option: %s", arg.c_str());
    return false;
  }

  if (new_profile_use) {
    if (ctx.args_info.profile_use) {
      cc_log("Multiple profiling options not supported");
      return false;
    }
    ctx.args_info.profile_use = true;
  }

  if (!new_profile_path.empty()) {
    ctx.args_info.profile_path = new_profile_path;
    cc_log("Set profile directory to %s", ctx.args_info.profile_path.c_str());
  }

  if (ctx.args_info.profile_generate && ctx.args_info.profile_use) {
    // Too hard to figure out what the compiler will do.
    cc_log("Both generating and using profile info, giving up");
    return false;
  }

  return true;
}

// Compiler in depend mode is invoked with the original arguments. Collect extra
// arguments that should be added.
void
add_extra_arg(Context& ctx, const std::string& arg)
{
  if (ctx.config.depend_mode()) {
    ctx.args_info.depend_extra_args.push_back(arg);
  }
}

optional<enum stats>
process_arg(Context& ctx,
            Args& args,
            size_t& args_index,
            ArgumentProcessingState& state)
{
  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  size_t& i = args_index;

  // The user knows best: just swallow the next arg.
  if (args[i] == "--ccache-skip") {
    i++;
    if (i == args.size()) {
      cc_log("--ccache-skip lacks an argument");
      return STATS_ARGS;
    }
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  // Special case for -E.
  if (args[i] == "-E" || args[i] == "/E") {
    return STATS_PREPROCESSING;
  }

  // Handle "@file" argument.
  if (Util::starts_with(args[i], "@") || Util::starts_with(args[i], "-@")) {
    const char* argpath = args[i].c_str() + 1;

    if (argpath[-1] == '-') {
      ++argpath;
    }
    auto file_args = Args::from_gcc_atfile(argpath);
    if (!file_args) {
      cc_log("Couldn't read arg file %s", argpath);
      return STATS_ARGS;
    }

    args.replace(i, *file_args);
    i--;
    return nullopt;
  }

  // Handle cuda "-optf" and "--options-file" argument.
  if (ctx.guessed_compiler == GuessedCompiler::nvcc
      && (args[i] == "-optf" || args[i] == "--options-file")) {
    if (i == args.size() - 1) {
      cc_log("Expected argument after %s", args[i].c_str());
      return STATS_ARGS;
    }
    ++i;

    // Argument is a comma-separated list of files.
    auto paths = Util::split_into_strings(args[i], ",");
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
      auto file_args = Args::from_gcc_atfile(*it);
      if (!file_args) {
        cc_log("Couldn't read CUDA options file %s", it->c_str());
        return STATS_ARGS;
      }

      args.insert(i + 1, *file_args);
    }

    return nullopt;
  }

  // These are always too hard.
  if (compopt_too_hard(args[i]) || Util::starts_with(args[i], "-fdump-")
      || Util::starts_with(args[i], "-MJ")) {
    cc_log("Compiler option %s is unsupported", args[i].c_str());
    return STATS_UNSUPPORTED_OPTION;
  }

  // These are too hard in direct mode.
  if (config.direct_mode() && compopt_too_hard_for_direct_mode(args[i])) {
    cc_log("Unsupported compiler option for direct mode: %s", args[i].c_str());
    config.set_direct_mode(false);
  }

  // -Xarch_* options are too hard.
  if (Util::starts_with(args[i], "-Xarch_")) {
    cc_log("Unsupported compiler option: %s", args[i].c_str());
    return STATS_UNSUPPORTED_OPTION;
  }

  // Handle -arch options.
  if (args[i] == "-arch") {
    ++i;
    args_info.arch_args.emplace_back(args[i]);
    if (args_info.arch_args.size() == 2) {
      config.set_run_second_cpp(true);
    }
    return nullopt;
  }

  // Handle options that should not be passed to the preprocessor.
  if (compopt_affects_comp(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    if (compopt_takes_arg(args[i])) {
      if (i == args.size() - 1) {
        cc_log("Missing argument to %s", args[i].c_str());
        return STATS_ARGS;
      }
      state.compiler_only_args.push_back(args[i + 1]);
      ++i;
    }
    return nullopt;
  }
  if (compopt_prefix_affects_comp(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    return nullopt;
  }

  if (args[i] == "-fpch-preprocess" || args[i] == "-emit-pch"
      || args[i] == "-emit-pth") {
    state.found_fpch_preprocess = true;
  }

  // Modules are handled on demand as necessary in the background, so there is
  // no need to cache them, they can in practice be ignored. All that is needed
  // is to correctly depend also on module.modulemap files, and those are
  // included only in depend mode (preprocessed output does not list them).
  // Still, not including the modules themselves in the hash could possibly
  // result in an object file that would be different from the actual
  // compilation (even though it should be compatible), so require a sloppiness
  // flag.
  if (args[i] == "-fmodules") {
    if (!config.depend_mode() || !config.direct_mode()) {
      cc_log("Compiler option %s is unsupported without direct depend mode",
             args[i].c_str());
      return STATS_CANTUSEMODULES;
    } else if (!(config.sloppiness() & SLOPPY_MODULES)) {
      cc_log(
        "You have to specify \"modules\" sloppiness when using"
        " -fmodules to get hits");
      return STATS_CANTUSEMODULES;
    }
  }

  // We must have -c.
  if (args[i] == "-c" || args[i] == "/c") {
    state.found_c_opt = true;
    return nullopt;
  }

  // when using nvcc with separable compilation, -dc implies -c
  if ((args[i] == "-dc" || args[i] == "--device-c")
      && ctx.guessed_compiler == GuessedCompiler::nvcc) {
    state.found_dc_opt = true;
    return nullopt;
  }

  // -S changes the default extension.
  if (args[i] == "-S") {
    state.common_args.push_back(args[i]);
    state.found_S_opt = true;
    return nullopt;
  }

  if (args[i].length() >= 3 && Util::starts_with(args[i], "-x")
      && !islower(args[i][2])) {
    // -xCODE (where CODE can be e.g. Host or CORE-AVX2, always starting with an
    // uppercase letter) is an ordinary Intel compiler option, not a language
    // specification. (GCC's "-x" language argument is always lowercase.)
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  // Special handling for -x: remember the last specified language before the
  // input file and strip all -x options from the arguments.
  if (args[i] == "-x") {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }
    if (args_info.input_file.empty()) {
      state.explicit_language = args[i + 1];
    }
    i++;
    return nullopt;
  }
  if (Util::starts_with(args[i], "-x")) {
    if (args_info.input_file.empty()) {
      state.explicit_language = args[i].substr(2);
    }
    return nullopt;
  }

  // We need to work out where the output was meant to go.
  if (args[i] == "-o") {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }
    args_info.output_obj = Util::make_relative_path(ctx, args[i + 1]);
    i++;
    return nullopt;
  }

  // Alternate form of -o with no space. Nvcc does not support this.
  if (Util::starts_with(args[i], "-o")
      && ctx.guessed_compiler != GuessedCompiler::nvcc) {
    args_info.output_obj =
      Util::make_relative_path(ctx, string_view(args[i]).substr(2));
    return nullopt;
  }

  // MSVC /Fo with no space.
  if (Util::starts_with(args[i], "/Fo")
      && ctx.guessed_compiler == GuessedCompiler::msvc) {
    args_info.output_obj =
      Util::make_relative_path(ctx, string_view(args[i]).substr(3));
    return nullopt;
  }

  if (Util::starts_with(args[i], "-fdebug-prefix-map=")
      || Util::starts_with(args[i], "-ffile-prefix-map=")) {
    std::string map = args[i].substr(args[i].find('=') + 1);
    args_info.debug_prefix_maps.push_back(map);
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  // Debugging is handled specially, so that we know if we can strip line
  // number info.
  if (Util::starts_with(args[i], "-g")) {
    state.common_args.push_back(args[i]);

    if (Util::starts_with(args[i], "-gdwarf")) {
      // Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
      // debug info on level 2.
      args_info.generating_debuginfo = true;
      return nullopt;
    }

    if (Util::starts_with(args[i], "-gz")) {
      // -gz[=type] neither disables nor enables debug info.
      return nullopt;
    }

    char last_char = args[i].back();
    if (last_char == '0') {
      // "-g0", "-ggdb0" or similar: All debug information disabled.
      args_info.generating_debuginfo = false;
      state.generating_debuginfo_level_3 = false;
    } else {
      args_info.generating_debuginfo = true;
      if (last_char == '3') {
        state.generating_debuginfo_level_3 = true;
      }
      if (args[i] == "-gsplit-dwarf") {
        args_info.seen_split_dwarf = true;
      }
    }
    return nullopt;
  }

  // These options require special handling, because they behave differently
  // with gcc -E, when the output file is not specified.
  if ((args[i] == "-MD" || args[i] == "-MMD")
      && ctx.guessed_compiler != GuessedCompiler::msvc) {
    args_info.generating_dependencies = true;
    state.dep_args.push_back(args[i]);
    return nullopt;
  }

  if (Util::starts_with(args[i], "-MF")) {
    state.dependency_filename_specified = true;

    std::string dep_file;
    bool separate_argument = (args[i].size() == 3);
    if (separate_argument) {
      // -MF arg
      if (i == args.size() - 1) {
        cc_log("Missing argument to %s", args[i].c_str());
        return STATS_ARGS;
      }
      dep_file = args[i + 1];
      i++;
    } else {
      // -MFarg or -MF=arg (EDG-based compilers)
      dep_file = args[i].substr(args[i][3] == '=' ? 4 : 3);
    }
    args_info.output_dep = Util::make_relative_path(ctx, dep_file);
    // Keep the format of the args the same.
    if (separate_argument) {
      state.dep_args.push_back("-MF");
      state.dep_args.push_back(args_info.output_dep);
    } else {
      state.dep_args.push_back("-MF" + args_info.output_dep);
    }
    return nullopt;
  }

  if (Util::starts_with(args[i], "-MQ") || Util::starts_with(args[i], "-MT")) {
    state.dependency_target_specified = true;

    if (args[i].size() == 3) {
      // -MQ arg or -MT arg
      if (i == args.size() - 1) {
        cc_log("Missing argument to %s", args[i].c_str());
        return STATS_ARGS;
      }
      state.dep_args.push_back(args[i]);
      std::string relpath = Util::make_relative_path(ctx, args[i + 1]);
      state.dep_args.push_back(relpath);
      i++;
    } else {
      auto arg_opt = string_view(args[i]).substr(0, 3);
      auto option = string_view(args[i]).substr(3);
      auto relpath = Util::make_relative_path(ctx, option);
      state.dep_args.push_back(fmt::format("{}{}", arg_opt, relpath));
    }
    return nullopt;
  }

  if (args[i] == "-fprofile-arcs") {
    args_info.profile_arcs = true;
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (args[i] == "-ftest-coverage") {
    args_info.generating_coverage = true;
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (args[i] == "-fstack-usage") {
    args_info.generating_stackusage = true;
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (args[i] == "--coverage"      // = -fprofile-arcs -ftest-coverage
      || args[i] == "-coverage") { // Undocumented but still works.
    args_info.profile_arcs = true;
    args_info.generating_coverage = true;
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (Util::starts_with(args[i], "-fprofile-")
      || Util::starts_with(args[i], "-fauto-profile")
      || args[i] == "-fbranch-probabilities") {
    if (!process_profiling_option(ctx, args[i])) {
      // The failure is logged by process_profiling_option.
      return STATS_UNSUPPORTED_OPTION;
    }
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (Util::starts_with(args[i], "-fsanitize-blacklist=")) {
    args_info.sanitize_blacklists.emplace_back(args[i].substr(21));
    state.common_args.push_back(args[i]);
    return nullopt;
  }

  if (Util::starts_with(args[i], "--sysroot=")) {
    auto path = string_view(args[i]).substr(10);
    auto relpath = Util::make_relative_path(ctx, path);
    state.common_args.push_back("--sysroot=" + relpath);
    return nullopt;
  }

  // Alternate form of specifying sysroot without =
  if (args[i] == "--sysroot") {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }
    state.common_args.push_back(args[i]);
    auto relpath = Util::make_relative_path(ctx, args[i + 1]);
    state.common_args.push_back(relpath);
    i++;
    return nullopt;
  }

  // Alternate form of specifying target without =
  if (args[i] == "-target") {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }
    state.common_args.push_back(args[i]);
    state.common_args.push_back(args[i + 1]);
    i++;
    return nullopt;
  }

  if (Util::starts_with(args[i], "-Wp,")) {
    if (args[i] == "-Wp,-P" || args[i].find(",-P,") != std::string::npos
        || Util::ends_with(args[i], ",-P")) {
      // -P removes preprocessor information in such a way that the object file
      // from compiling the preprocessed file will not be equal to the object
      // file produced when compiling without ccache.
      cc_log("Too hard option -Wp,-P detected");
      return STATS_UNSUPPORTED_OPTION;
    } else if (Util::starts_with(args[i], "-Wp,-MD,")
               && args[i].find(',', 8) == std::string::npos) {
      args_info.generating_dependencies = true;
      state.dependency_filename_specified = true;
      args_info.output_dep =
        Util::make_relative_path(ctx, string_view(args[i]).substr(8));
      state.dep_args.push_back(args[i]);
      return nullopt;
    } else if (Util::starts_with(args[i], "-Wp,-MMD,")
               && args[i].find(',', 9) == std::string::npos) {
      args_info.generating_dependencies = true;
      state.dependency_filename_specified = true;
      args_info.output_dep =
        Util::make_relative_path(ctx, string_view(args[i]).substr(9));
      state.dep_args.push_back(args[i]);
      return nullopt;
    } else if (Util::starts_with(args[i], "-Wp,-D")
               && args[i].find(',', 6) == std::string::npos) {
      // Treat it like -D.
      state.cpp_args.push_back(args[i].substr(4));
      return nullopt;
    } else if (args[i] == "-Wp,-MP"
               || (args[i].size() > 8 && Util::starts_with(args[i], "-Wp,-M")
                   && args[i][7] == ','
                   && (args[i][6] == 'F' || args[i][6] == 'Q'
                       || args[i][6] == 'T')
                   && args[i].find(',', 8) == std::string::npos)) {
      // TODO: Make argument to MF/MQ/MT relative.
      state.dep_args.push_back(args[i]);
      return nullopt;
    } else if (config.direct_mode()) {
      // -Wp, can be used to pass too hard options to the preprocessor.
      // Hence, disable direct mode.
      cc_log("Unsupported compiler option for direct mode: %s",
             args[i].c_str());
      config.set_direct_mode(false);
    }

    // Any other -Wp,* arguments are only relevant for the preprocessor.
    state.cpp_args.push_back(args[i]);
    return nullopt;
  }

  if (args[i] == "-MP") {
    state.dep_args.push_back(args[i]);
    return nullopt;
  }

  // Input charset needs to be handled specially.
  if (Util::starts_with(args[i], "-finput-charset=")) {
    state.input_charset = args[i];
    return nullopt;
  }

  if (args[i] == "--serialize-diagnostics") {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }
    args_info.generating_diagnostics = true;
    args_info.output_dia = Util::make_relative_path(ctx, args[i + 1]);
    i++;
    return nullopt;
  }

  if (args[i] == "-fcolor-diagnostics" || args[i] == "-fno-color-diagnostics"
      || args[i] == "-fdiagnostics-color"
      || args[i] == "-fdiagnostics-color=always"
      || args[i] == "-fno-diagnostics-color"
      || args[i] == "-fdiagnostics-color=never") {
    state.common_args.push_back(args[i]);
    state.found_color_diagnostics = true;
    return nullopt;
  }

  if (args[i] == "-fdiagnostics-color=auto") {
    if (color_output_possible()) {
      // Output is redirected, so color output must be forced.
      state.common_args.push_back("-fdiagnostics-color=always");
      add_extra_arg(ctx, "-fdiagnostics-color=always");
      cc_log("Automatically forcing colors");
    } else {
      state.common_args.push_back(args[i]);
    }
    state.found_color_diagnostics = true;
    return nullopt;
  }

  // GCC
  if (args[i] == "-fdirectives-only") {
    state.found_directives_only = true;
    return nullopt;
  }

  // Clang
  if (args[i] == "-frewrite-includes") {
    state.found_rewrite_includes = true;
    return nullopt;
  }

  if (config.sloppiness() & SLOPPY_CLANG_INDEX_STORE
      && args[i] == "-index-store-path") {
    // Xcode 9 or later calls Clang with this option. The given path includes a
    // UUID that might lead to cache misses, especially when cache is shared
    // among multiple users.
    i++;
    if (i <= args.size() - 1) {
      cc_log("Skipping argument -index-store-path %s", args[i].c_str());
    }
    return nullopt;
  }

  // Options taking an argument that we may want to rewrite to relative paths to
  // get better hit rate. A secondary effect is that paths in the standard error
  // output produced by the compiler will be normalized.
  if (compopt_takes_path(args[i])) {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }

    if (!detect_pch(ctx, args[i], args[i + 1], &state.found_pch)) {
      return STATS_ARGS;
    }

    std::string relpath = Util::make_relative_path(ctx, args[i + 1]);
    if (compopt_affects_cpp(args[i])) {
      state.cpp_args.push_back(args[i]);
      state.cpp_args.push_back(relpath);
    } else {
      state.common_args.push_back(args[i]);
      state.common_args.push_back(relpath);
    }

    i++;
    return nullopt;
  }

  // Same as above but options with concatenated argument beginning with a
  // slash.
  if (args[i][0] == '-'
      || (ctx.guessed_compiler == GuessedCompiler::msvc && args[i][0] == '/')) {
    size_t slash_pos = args[i].find('/');
    if (slash_pos != std::string::npos) {
      std::string option = args[i].substr(0, slash_pos);
      if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
        auto relpath =
          Util::make_relative_path(ctx, string_view(args[i]).substr(slash_pos));
        std::string new_option = option + relpath;
        if (compopt_affects_cpp(option)) {
          state.cpp_args.push_back(new_option);
        } else {
          state.common_args.push_back(new_option);
        }
        return nullopt;
      }
    }
  }

  // Options that take an argument.
  if (compopt_takes_arg(args[i])) {
    if (i == args.size() - 1) {
      cc_log("Missing argument to %s", args[i].c_str());
      return STATS_ARGS;
    }

    if (compopt_affects_cpp(args[i])) {
      state.cpp_args.push_back(args[i]);
      state.cpp_args.push_back(args[i + 1]);
    } else {
      state.common_args.push_back(args[i]);
      state.common_args.push_back(args[i + 1]);
    }

    i++;
    return nullopt;
  }

  // Other options.
  if (args[i][0] == '-'
      || (ctx.guessed_compiler == GuessedCompiler::msvc && args[i][0] == '/')) {
    if (compopt_affects_cpp(args[i]) || compopt_prefix_affects_cpp(args[i])) {
      state.cpp_args.push_back(args[i]);
    } else {
      state.common_args.push_back(args[i]);
    }
    return nullopt;
  }

  // If an argument isn't a plain file then assume its an option, not an input
  // file. This allows us to cope better with unusual compiler options.
  //
  // Note that "/dev/null" is an exception that is sometimes used as an input
  // file when code is testing compiler flags.
  if (args[i] != "/dev/null") {
    auto st = Stat::stat(args[i]);
    if (!st || !st.is_regular()) {
      cc_log("%s is not a regular file, not considering as input file",
             args[i].c_str());
      state.common_args.push_back(args[i]);
      return nullopt;
    }
  }

  if (!args_info.input_file.empty()) {
    if (!language_for_file(args[i]).empty()) {
      cc_log("Multiple input files: %s and %s",
             args_info.input_file.c_str(),
             args[i].c_str());
      return STATS_MULTIPLE;
    } else if (!state.found_c_opt && !state.found_dc_opt) {
      cc_log("Called for link with %s", args[i].c_str());
      if (args[i].find("conftest.") != std::string::npos) {
        return STATS_CONFTEST;
      } else {
        return STATS_LINK;
      }
    } else {
      cc_log("Unsupported source extension: %s", args[i].c_str());
      return STATS_SOURCELANG;
    }
  }

  // The source code file path gets put into the notes.
  if (args_info.generating_coverage) {
    args_info.input_file = args[i];
    return nullopt;
  }

  // Rewrite to relative to increase hit rate.
  args_info.input_file = Util::make_relative_path(ctx, args[i]);

  return nullopt;
}

void
handle_dependency_environment_variables(Context& ctx,
                                        ArgumentProcessingState& state)
{
  ArgsInfo& args_info = ctx.args_info;

  // See <http://gcc.gnu.org/onlinedocs/cpp/Environment-Variables.html>.
  // Contrary to what the documentation seems to imply the compiler still
  // creates object files with these defined (confirmed with GCC 8.2.1), i.e.
  // they work as -MMD/-MD, not -MM/-M. These environment variables do nothing
  // on Clang.
  const char* dependencies_env = getenv("DEPENDENCIES_OUTPUT");
  bool using_sunpro_dependencies = false;
  if (!dependencies_env) {
    dependencies_env = getenv("SUNPRO_DEPENDENCIES");
    using_sunpro_dependencies = true;
  }
  if (!dependencies_env) {
    return;
  }

  args_info.generating_dependencies = true;
  state.dependency_filename_specified = true;

  auto dependencies = Util::split_into_views(dependencies_env, " ");

  if (!dependencies.empty()) {
    auto abspath_file = dependencies[0];
    args_info.output_dep = Util::make_relative_path(ctx, abspath_file);
  }

  // Specifying target object is optional.
  if (dependencies.size() > 1) {
    // It's the "file target" form.
    state.dependency_target_specified = true;
    string_view abspath_obj = dependencies[1];
    std::string relpath_obj = Util::make_relative_path(ctx, abspath_obj);
    // Ensure that the compiler gets a relative path.
    std::string relpath_both =
      fmt::format("{} {}", args_info.output_dep, relpath_obj);
    if (using_sunpro_dependencies) {
      x_setenv("SUNPRO_DEPENDENCIES", relpath_both.c_str());
    } else {
      x_setenv("DEPENDENCIES_OUTPUT", relpath_both.c_str());
    }
  } else {
    // It's the "file" form.
    state.dependency_implicit_target_specified = true;
    // Ensure that the compiler gets a relative path.
    if (using_sunpro_dependencies) {
      x_setenv("SUNPRO_DEPENDENCIES", args_info.output_dep.c_str());
    } else {
      x_setenv("DEPENDENCIES_OUTPUT", args_info.output_dep.c_str());
    }
  }
}

} // namespace

optional<enum stats>
process_args(Context& ctx,
             Args& preprocessor_args,
             Args& extra_args_to_hash,
             Args& compiler_args)
{
  assert(!ctx.orig_args.empty());

  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  // args is a copy of the original arguments given to the compiler but with
  // arguments from @file and similar constructs expanded. It's only used as a
  // temporary data structure to loop over.
  Args args = ctx.orig_args;
  ArgumentProcessingState state;

  state.common_args.push_back(args[0]); // Compiler

  for (size_t i = 1; i < args.size(); i++) {
    auto error = process_arg(ctx, args, i, state);
    if (error) {
      return error;
    }
  }

  if (state.generating_debuginfo_level_3 && !config.run_second_cpp()) {
    cc_log("Generating debug info level 3; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  handle_dependency_environment_variables(ctx, state);

  if (args_info.input_file.empty()) {
    cc_log("No input file found");
    return STATS_NOINPUT;
  }

  if (state.found_pch || state.found_fpch_preprocess) {
    args_info.using_precompiled_header = true;
    if (!(config.sloppiness() & SLOPPY_TIME_MACROS)) {
      cc_log(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      cc_log("Disabling direct mode");
      return STATS_CANTUSEPCH;
    }
  }

  if (args_info.profile_path.empty()) {
    args_info.profile_path = ctx.apparent_cwd;
  }

  if (!state.explicit_language.empty() && state.explicit_language == "none") {
    state.explicit_language.clear();
  }
  state.file_language = language_for_file(args_info.input_file);
  if (!state.explicit_language.empty()) {
    if (!language_is_supported(state.explicit_language)) {
      cc_log("Unsupported language: %s", state.explicit_language.c_str());
      return STATS_SOURCELANG;
    }
    args_info.actual_language = state.explicit_language;
  } else {
    args_info.actual_language = state.file_language;
  }

  args_info.output_is_precompiled_header =
    args_info.actual_language.find("-header") != std::string::npos;

  if (args_info.output_is_precompiled_header
      && !(config.sloppiness() & SLOPPY_PCH_DEFINES)) {
    cc_log(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    return STATS_CANTUSEPCH;
  }

  if (!state.found_c_opt && !state.found_dc_opt && !state.found_S_opt) {
    if (args_info.output_is_precompiled_header) {
      state.common_args.push_back("-c");
    } else {
      cc_log("No -c option found");
      // Having a separate statistic for autoconf tests is useful, as they are
      // the dominant form of "called for link" in many cases.
      return args_info.input_file.find("conftest.") != std::string::npos
               ? STATS_CONFTEST
               : STATS_LINK;
    }
  }

  if (args_info.actual_language.empty()) {
    cc_log("Unsupported source extension: %s", args_info.input_file.c_str());
    return STATS_SOURCELANG;
  }

  if (!config.run_second_cpp() && args_info.actual_language == "cu") {
    cc_log("Using CUDA compiler; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  args_info.direct_i_file = language_is_preprocessed(args_info.actual_language);

  if (args_info.output_is_precompiled_header && !config.run_second_cpp()) {
    // It doesn't work to create the .gch from preprocessed source.
    cc_log("Creating precompiled header; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  if (config.cpp_extension().empty()) {
    std::string p_language = p_language_for_language(args_info.actual_language);
    config.set_cpp_extension(extension_for_language(p_language).substr(1));
  }

  // Don't try to second guess the compilers heuristics for stdout handling.
  if (args_info.output_obj == "-") {
    cc_log("Output file is -");
    return STATS_OUTSTDOUT;
  }

  if (args_info.output_obj.empty()) {
    if (args_info.output_is_precompiled_header) {
      args_info.output_obj = args_info.input_file + ".gch";
    } else {
      string_view extension = state.found_S_opt ? ".s" : ".o";
      args_info.output_obj = Util::change_extension(
        Util::base_name(args_info.input_file), extension);
    }
  }

  if (args_info.seen_split_dwarf) {
    size_t pos = args_info.output_obj.rfind('.');
    if (pos == std::string::npos || pos == args_info.output_obj.size() - 1) {
      cc_log("Badly formed object filename");
      return STATS_ARGS;
    }

    args_info.output_dwo = Util::change_extension(args_info.output_obj, ".dwo");
  }

  // Cope with -o /dev/null.
  if (args_info.output_obj != "/dev/null") {
    auto st = Stat::stat(args_info.output_obj);
    if (st && !st.is_regular()) {
      cc_log("Not a regular file: %s", args_info.output_obj.c_str());
      return STATS_BADOUTPUTFILE;
    }
  }

  auto output_dir = std::string(Util::dir_name(args_info.output_obj));
  auto st = Stat::stat(output_dir);
  if (!st || !st.is_directory()) {
    cc_log("Directory does not exist: %s", output_dir.c_str());
    return STATS_BADOUTPUTFILE;
  }

  // Some options shouldn't be passed to the real compiler when it compiles
  // preprocessed code:
  //
  // -finput-charset=XXX (otherwise conversion happens twice)
  // -x XXX (otherwise the wrong language is selected)
  if (!state.input_charset.empty()) {
    state.cpp_args.push_back(state.input_charset);
  }
  if (state.found_pch) {
    state.cpp_args.push_back("-fpch-preprocess");
  }
  if (!state.explicit_language.empty()) {
    state.cpp_args.push_back("-x");
    state.cpp_args.push_back(state.explicit_language);
  }

  // Since output is redirected, compilers will not color their output by
  // default, so force it explicitly if it would be otherwise done.
  if (!state.found_color_diagnostics && color_output_possible()) {
    if (ctx.guessed_compiler == GuessedCompiler::clang) {
      if (args_info.actual_language != "assembler") {
        state.common_args.push_back("-fcolor-diagnostics");
        add_extra_arg(ctx, "-fcolor-diagnostics");
        cc_log("Automatically enabling colors");
      }
    } else if (ctx.guessed_compiler == GuessedCompiler::gcc) {
      // GCC has it since 4.9, but that'd require detecting what GCC version is
      // used for the actual compile. However it requires also GCC_COLORS to be
      // set (and not empty), so use that for detecting if GCC would use colors.
      const char* gcc_colors = getenv("GCC_COLORS");
      if (gcc_colors && gcc_colors[0] != '\0') {
        state.common_args.push_back("-fdiagnostics-color");
        add_extra_arg(ctx, "-fdiagnostics-color");
        cc_log("Automatically enabling colors");
      }
    }
  }

  if (args_info.generating_dependencies) {
    if (!state.dependency_filename_specified) {
      auto default_depfile_name =
        Util::change_extension(args_info.output_obj, ".d");
      args_info.output_dep =
        Util::make_relative_path(ctx, default_depfile_name);
      if (!config.run_second_cpp()) {
        // If we're compiling preprocessed code we're sending dep_args to the
        // preprocessor so we need to use -MF to write to the correct .d file
        // location since the preprocessor doesn't know the final object path.
        state.dep_args.push_back("-MF");
        state.dep_args.push_back(default_depfile_name);
      }
    }

    if (!state.dependency_target_specified
        && !state.dependency_implicit_target_specified
        && !config.run_second_cpp()) {
      // If we're compiling preprocessed code we're sending dep_args to the
      // preprocessor so we need to use -MQ to get the correct target object
      // file in the .d file.
      state.dep_args.push_back("-MQ");
      state.dep_args.push_back(args_info.output_obj);
    }
  }

  if (args_info.generating_coverage) {
    auto gcda_path = Util::change_extension(args_info.output_obj, ".gcno");
    args_info.output_cov = Util::make_relative_path(ctx, gcda_path);
  }

  if (args_info.generating_stackusage) {
    auto default_sufile_name =
      Util::change_extension(args_info.output_obj, ".su");
    args_info.output_su = Util::make_relative_path(ctx, default_sufile_name);
  }

  compiler_args = state.common_args;
  compiler_args.push_back(state.compiler_only_args);

  if (config.run_second_cpp()) {
    compiler_args.push_back(state.cpp_args);
  } else if (state.found_directives_only || state.found_rewrite_includes) {
    // Need to pass the macros and any other preprocessor directives again.
    compiler_args.push_back(state.cpp_args);
    if (state.found_directives_only) {
      state.cpp_args.push_back("-fdirectives-only");
      // The preprocessed source code still needs some more preprocessing.
      compiler_args.push_back("-fpreprocessed");
      compiler_args.push_back("-fdirectives-only");
    }
    if (state.found_rewrite_includes) {
      state.cpp_args.push_back("-frewrite-includes");
      // The preprocessed source code still needs some more preprocessing.
      compiler_args.push_back("-x");
      compiler_args.push_back(args_info.actual_language);
    }
  } else if (!state.explicit_language.empty()) {
    // Workaround for a bug in Apple's patched distcc -- it doesn't properly
    // reset the language specified with -x, so if -x is given, we have to
    // specify the preprocessed language explicitly.
    compiler_args.push_back("-x");
    compiler_args.push_back(p_language_for_language(state.explicit_language));
  }

  if (state.found_c_opt) {
    compiler_args.push_back("-c");
  }

  if (state.found_dc_opt) {
    compiler_args.push_back("-dc");
  }

  for (const auto& arch : args_info.arch_args) {
    compiler_args.push_back("-arch");
    compiler_args.push_back(arch);
  }

  preprocessor_args = state.common_args;
  preprocessor_args.push_back(state.cpp_args);

  if (config.run_second_cpp()) {
    // When not compiling the preprocessed source code, only pass dependency
    // arguments to the compiler to avoid having to add -MQ, supporting e.g.
    // EDG-based compilers which don't support -MQ.
    compiler_args.push_back(state.dep_args);
  } else {
    // When compiling the preprocessed source code, pass dependency arguments to
    // the preprocessor since the compiler doesn't produce a .d file when
    // compiling preprocessed source code.
    preprocessor_args.push_back(state.dep_args);
  }

  extra_args_to_hash = state.compiler_only_args;
  if (config.run_second_cpp()) {
    extra_args_to_hash.push_back(state.dep_args);
  }

  return nullopt;
}
