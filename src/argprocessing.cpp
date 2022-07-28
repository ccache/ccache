// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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
#include "Logging.hpp"
#include "assertions.hpp"
#include "compopt.hpp"
#include "fmtmacros.hpp"
#include "language.hpp"

#include <Depfile.hpp>
#include <core/wincompat.hpp>
#include <util/string.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <cassert>

using core::Statistic;

namespace {

enum class ColorDiagnostics : int8_t { never, automatic, always };

// The dependency target in the dependency file is taken from the highest
// priority source.
enum class OutputDepOrigin : uint8_t {
  // Not set
  none = 0,
  // From -MF target
  mf = 1,
  // From -Wp,-MD,target or -Wp,-MMD,target
  wp = 2
};

struct ArgumentProcessingState
{
  bool found_c_opt = false;
  bool found_dc_opt = false;
  bool found_S_opt = false;
  bool found_pch = false;
  bool found_fpch_preprocess = false;
  bool found_Yu = false;
  bool found_valid_Fp = false;
  bool found_syntax_only = false;
  ColorDiagnostics color_diagnostics = ColorDiagnostics::automatic;
  bool found_directives_only = false;
  bool found_rewrite_includes = false;
  std::optional<std::string> found_xarch_arch;
  bool found_mf_opt = false;
  bool found_wp_md_or_mmd_opt = false;
  bool found_md_or_mmd_opt = false;

  std::string explicit_language;    // As specified with -x.
  std::string input_charset_option; // -finput-charset=...

  // Is the dependency file set via -Wp,-M[M]D,target or -MFtarget?
  OutputDepOrigin output_dep_origin = OutputDepOrigin::none;

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

  // compiler_only_args_no_hash contains arguments that should only be passed to
  // the compiler, not the preprocessor, and that also should not be part of the
  // hash identifying the result.
  Args compiler_only_args_no_hash;

  // Whether to include the full command line in the hash.
  bool hash_full_command_line = false;

  // Whether to include the actual CWD in the hash.
  bool hash_actual_cwd = false;
};

bool
color_output_possible()
{
  const char* term_env = getenv("TERM");
  return isatty(STDERR_FILENO) && term_env
         && Util::to_lowercase(term_env) != "dumb";
}

bool
detect_pch(const std::string& option,
           const std::string& arg,
           std::string& included_pch_file,
           bool is_cc1_option,
           ArgumentProcessingState& state)
{
  // Try to be smart about detecting precompiled headers.
  // If the option is an option for Clang (is_cc1_option), don't accept
  // anything just because it has a corresponding precompiled header,
  // because Clang doesn't behave that way either.
  std::string pch_file;
  if (option == "-Yu") {
    state.found_Yu = true;
    if (state.found_valid_Fp) { // Use file set by -Fp.
      LOG("Detected use of precompiled header: {}", included_pch_file);
      pch_file = included_pch_file;
      included_pch_file.clear(); // reset pch file set from /Fp
    } else {
      std::string file = Util::change_extension(arg, ".pch");
      if (Stat::stat(file)) {
        LOG("Detected use of precompiled header: {}", file);
        pch_file = file;
      }
    }
  } else if (option == "-Fp") {
    std::string file = arg;
    if (Util::get_extension(file).empty()) {
      file += ".pch";
    }
    if (Stat::stat(file)) {
      state.found_valid_Fp = true;
      if (!state.found_Yu) {
        LOG("Precompiled header file specified: {}", file);
        included_pch_file = file; // remember file
        return true;              // -Fp does not turn on PCH
      }
      LOG("Detected use of precompiled header: {}", file);
      pch_file = file;
      included_pch_file.clear(); // reset pch file set from /Yu
      // continue and set as if the file was passed to -Yu
    }
  } else if (option == "-include-pch" || option == "-include-pth") {
    if (Stat::stat(arg)) {
      LOG("Detected use of precompiled header: {}", arg);
      pch_file = arg;
    }
  } else if (!is_cc1_option) {
    for (const auto& extension : {".gch", ".pch", ".pth"}) {
      std::string path = arg + extension;
      if (Stat::stat(path)) {
        LOG("Detected use of precompiled header: {}", path);
        pch_file = path;
      }
    }
  }

  if (!pch_file.empty()) {
    if (!included_pch_file.empty()) {
      LOG("Multiple precompiled headers used: {} and {}",
          included_pch_file,
          pch_file);
      return false;
    }
    included_pch_file = pch_file;
    state.found_pch = true;
  }
  return true;
}

bool
process_profiling_option(const Context& ctx,
                         ArgsInfo& args_info,
                         const std::string& arg)
{
  static const std::vector<std::string> known_simple_options = {
    "-fprofile-correction",
    "-fprofile-reorder-functions",
    "-fprofile-sample-accurate",
    "-fprofile-values",
  };

  if (std::find(known_simple_options.begin(), known_simple_options.end(), arg)
      != known_simple_options.end()) {
    return true;
  }

  std::string new_profile_path;
  bool new_profile_use = false;

  if (util::starts_with(arg, "-fprofile-dir=")) {
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else if (arg == "-fprofile-generate" || arg == "-fprofile-instr-generate") {
    args_info.profile_generate = true;
    if (ctx.config.is_compiler_group_clang()) {
      new_profile_path = ".";
    } else {
      // GCC uses $PWD/$(basename $obj).
      new_profile_path = ctx.apparent_cwd;
    }
  } else if (util::starts_with(arg, "-fprofile-generate=")
             || util::starts_with(arg, "-fprofile-instr-generate=")) {
    args_info.profile_generate = true;
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else if (arg == "-fprofile-use" || arg == "-fprofile-instr-use"
             || arg == "-fprofile-sample-use" || arg == "-fbranch-probabilities"
             || arg == "-fauto-profile") {
    new_profile_use = true;
    if (args_info.profile_path.empty()) {
      new_profile_path = ".";
    }
  } else if (util::starts_with(arg, "-fprofile-use=")
             || util::starts_with(arg, "-fprofile-instr-use=")
             || util::starts_with(arg, "-fprofile-sample-use=")
             || util::starts_with(arg, "-fauto-profile=")) {
    new_profile_use = true;
    new_profile_path = arg.substr(arg.find('=') + 1);
  } else {
    LOG("Unknown profiling option: {}", arg);
    return false;
  }

  if (new_profile_use) {
    if (args_info.profile_use) {
      LOG_RAW("Multiple profiling options not supported");
      return false;
    }
    args_info.profile_use = true;
  }

  if (!new_profile_path.empty()) {
    args_info.profile_path = new_profile_path;
    LOG("Set profile directory to {}", args_info.profile_path);
  }

  if (args_info.profile_generate && args_info.profile_use) {
    // Too hard to figure out what the compiler will do.
    LOG_RAW("Both generating and using profile info, giving up");
    return false;
  }

  return true;
}

// Returns std::nullopt if the option wasn't recognized, otherwise the error
// code (with Statistic::none for "no error").
std::optional<Statistic>
process_option_arg(const Context& ctx,
                   ArgsInfo& args_info,
                   Config& config,
                   Args& args,
                   size_t& args_index,
                   ArgumentProcessingState& state)
{
  size_t& i = args_index;
  // The user knows best: just swallow the next arg.
  if (args[i] == "--ccache-skip") {
    i++;
    if (i == args.size()) {
      LOG_RAW("--ccache-skip lacks an argument");
      return Statistic::bad_compiler_arguments;
    }
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  bool changed_from_slash = false;
  if (ctx.config.is_compiler_group_msvc() && util::starts_with(args[i], "/")) {
    // MSVC understands both /option and -option, so convert all /option to
    // -option to simplify our handling.
    args[i][0] = '-';
    changed_from_slash = true;
  }

  // Ignore clang -ivfsoverlay <arg> to not detect multiple input files.
  if (args[i] == "-ivfsoverlay"
      && !(config.sloppiness().is_enabled(core::Sloppy::ivfsoverlay))) {
    LOG_RAW(
      "You have to specify \"ivfsoverlay\" sloppiness when using"
      " -ivfsoverlay to get hits");
    return Statistic::unsupported_compiler_option;
  }

  // Special case for -E.
  if (args[i] == "-E") {
    return Statistic::called_for_preprocessing;
  }
  // MSVC -P is -E with output to a file.
  if (args[i] == "-P" && ctx.config.is_compiler_group_msvc()) {
    return Statistic::called_for_preprocessing;
  }

  // Handle "@file" argument.
  if (util::starts_with(args[i], "@") || util::starts_with(args[i], "-@")) {
    const char* argpath = args[i].c_str() + 1;

    if (argpath[-1] == '-') {
      ++argpath;
    }
    auto file_args = Args::from_atfile(argpath,
                                       config.is_compiler_group_msvc()
                                         ? Args::AtFileFormat::msvc
                                         : Args::AtFileFormat::gcc);
    if (!file_args) {
      LOG("Couldn't read arg file {}", argpath);
      return Statistic::bad_compiler_arguments;
    }

    args.replace(i, *file_args);
    i--;
    return Statistic::none;
  }

  // Handle cuda "-optf" and "--options-file" argument.
  if (config.compiler_type() == CompilerType::nvcc
      && (args[i] == "-optf" || args[i] == "--options-file")) {
    if (i == args.size() - 1) {
      LOG("Expected argument after {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    ++i;

    // Argument is a comma-separated list of files.
    auto paths = Util::split_into_strings(args[i], ",");
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
      auto file_args = Args::from_atfile(*it);
      if (!file_args) {
        LOG("Couldn't read CUDA options file {}", *it);
        return Statistic::bad_compiler_arguments;
      }

      args.insert(i + 1, *file_args);
    }

    return Statistic::none;
  }

  // These are always too hard.
  if (compopt_too_hard(args[i]) || util::starts_with(args[i], "-fdump-")
      || util::starts_with(args[i], "-MJ")
      || util::starts_with(args[i], "-Yc")) {
    LOG("Compiler option {} is unsupported", args[i]);
    return Statistic::unsupported_compiler_option;
  }

  // These are too hard in direct mode.
  if (config.direct_mode() && compopt_too_hard_for_direct_mode(args[i])) {
    LOG("Unsupported compiler option for direct mode: {}", args[i]);
    config.set_direct_mode(false);
  }

  // -Xarch_* options are too hard.
  if (util::starts_with(args[i], "-Xarch_")) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    const auto arch = args[i].substr(7);
    if (!state.found_xarch_arch) {
      state.found_xarch_arch = arch;
    } else if (*state.found_xarch_arch != arch) {
      LOG_RAW("Multiple different -Xarch_* options not supported");
      return Statistic::unsupported_compiler_option;
    }
    state.common_args.push_back(args[i]);
    state.common_args.push_back(args[i + 1]);
    ++i;
    return Statistic::none;
  }

  // Handle -arch options.
  if (args[i] == "-arch") {
    ++i;
    args_info.arch_args.emplace_back(args[i]);
    if (args_info.arch_args.size() == 2) {
      config.set_run_second_cpp(true);
    }
    return Statistic::none;
  }

  // Some arguments that clang passes directly to cc1 (related to precompiled
  // headers) need the usual ccache handling. In those cases, the -Xclang
  // prefix is skipped and the cc1 argument is handled instead.
  if (args[i] == "-Xclang" && i + 1 < args.size()
      && (args[i + 1] == "-emit-pch" || args[i + 1] == "-emit-pth"
          || args[i + 1] == "-include-pch" || args[i + 1] == "-include-pth"
          || args[i + 1] == "-fno-pch-timestamp")) {
    if (compopt_affects_compiler_output(args[i + 1])) {
      state.compiler_only_args.push_back(args[i]);
    } else if (compopt_affects_cpp_output(args[i + 1])) {
      state.cpp_args.push_back(args[i]);
    } else {
      state.common_args.push_back(args[i]);
    }
    ++i;
  }

  if (util::starts_with(args[i], "-Wa,")
      && args[i].find('=') != std::string::npos) {
    LOG("Assembler listing file (-Wa,...=file) is currently not supported: {}",
        args[i]);
    return Statistic::unsupported_compiler_option;
  }

  // Handle options that should not be passed to the preprocessor.
  if (compopt_affects_compiler_output(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    if (compopt_takes_arg(args[i])
        || (config.compiler_type() == CompilerType::nvcc
            && args[i] == "-Werror")) {
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.compiler_only_args.push_back(args[i + 1]);
      ++i;
    }
    return Statistic::none;
  }
  if (compopt_prefix_affects_compiler_output(args[i])) {
    state.compiler_only_args.push_back(args[i]);
    return Statistic::none;
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
      LOG("Compiler option {} is unsupported without direct depend mode",
          args[i]);
      return Statistic::could_not_use_modules;
    } else if (!(config.sloppiness().is_enabled(core::Sloppy::modules))) {
      LOG_RAW(
        "You have to specify \"modules\" sloppiness when using"
        " -fmodules to get hits");
      return Statistic::could_not_use_modules;
    }
  }

  // We must have -c.
  if (args[i] == "-c") {
    state.found_c_opt = true;
    return Statistic::none;
  }

  // MSVC -Fo with no space.
  if (util::starts_with(args[i], "-Fo") && config.is_compiler_group_msvc()) {
    args_info.output_obj = args[i].substr(3);
    return Statistic::none;
  }

  // when using nvcc with separable compilation, -dc implies -c
  if ((args[i] == "-dc" || args[i] == "--device-c")
      && config.compiler_type() == CompilerType::nvcc) {
    state.found_dc_opt = true;
    return Statistic::none;
  }

  // -S changes the default extension.
  if (args[i] == "-S") {
    state.common_args.push_back(args[i]);
    state.found_S_opt = true;
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-x")) {
    if (args[i].length() >= 3 && !islower(args[i][2])) {
      // -xCODE (where CODE can be e.g. Host or CORE-AVX2, always starting with
      // an uppercase letter) is an ordinary Intel compiler option, not a
      // language specification. (GCC's "-x" language argument is always
      // lowercase.)
      state.common_args.push_back(args[i]);
      return Statistic::none;
    }

    // Special handling for -x: remember the last specified language before the
    // input file and strip all -x options from the arguments.
    if (args[i].length() == 2) {
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      if (args_info.input_file.empty()) {
        state.explicit_language = args[i + 1];
      }
      i++;
      return Statistic::none;
    }

    DEBUG_ASSERT(args[i].length() >= 3);
    if (args_info.input_file.empty()) {
      state.explicit_language = args[i].substr(2);
    }
    return Statistic::none;
  }

  // We need to work out where the output was meant to go.
  if (args[i] == "-o") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    args_info.output_obj = args[i + 1];
    i++;
    return Statistic::none;
  }

  // Alternate form of -o with no space. Nvcc does not support this.
  // Cl does support it as deprecated, but also has -openmp or -link -out
  // which can confuse this and cause incorrect output_obj (and thus
  // ccache debug file location), so better ignore it.
  if (util::starts_with(args[i], "-o")
      && config.compiler_type() != CompilerType::nvcc
      && config.compiler_type() != CompilerType::msvc) {
    args_info.output_obj = args[i].substr(2);
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-fdebug-prefix-map=")
      || util::starts_with(args[i], "-ffile-prefix-map=")) {
    std::string map = args[i].substr(args[i].find('=') + 1);
    args_info.debug_prefix_maps.push_back(map);
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  // Debugging is handled specially, so that we know if we can strip line
  // number info.
  if (util::starts_with(args[i], "-g")) {
    state.common_args.push_back(args[i]);

    if (util::starts_with(args[i], "-gdwarf")) {
      // Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
      // debug info on level 2.
      args_info.generating_debuginfo = true;
      return Statistic::none;
    }

    if (util::starts_with(args[i], "-gz")) {
      // -gz[=type] neither disables nor enables debug info.
      return Statistic::none;
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
    return Statistic::none;
  }

  // These options require special handling, because they behave differently
  // with gcc -E, when the output file is not specified.
  if ((args[i] == "-MD" || args[i] == "-MMD")
      && !config.is_compiler_group_msvc()) {
    state.found_md_or_mmd_opt = true;
    args_info.generating_dependencies = true;
    state.dep_args.push_back(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-MF")) {
    state.found_mf_opt = true;

    std::string dep_file;
    bool separate_argument = (args[i].size() == 3);
    if (separate_argument) {
      // -MF arg
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      dep_file = args[i + 1];
      i++;
    } else {
      // -MFarg or -MF=arg (EDG-based compilers)
      dep_file = args[i].substr(args[i][3] == '=' ? 4 : 3);
    }

    if (state.output_dep_origin <= OutputDepOrigin::mf) {
      state.output_dep_origin = OutputDepOrigin::mf;
      args_info.output_dep = Util::make_relative_path(ctx, dep_file);
    }
    // Keep the format of the args the same.
    if (separate_argument) {
      state.dep_args.push_back("-MF");
      state.dep_args.push_back(args_info.output_dep);
    } else {
      state.dep_args.push_back("-MF" + args_info.output_dep);
    }
    return Statistic::none;
  }

  if ((util::starts_with(args[i], "-MQ") || util::starts_with(args[i], "-MT"))
      && !config.is_compiler_group_msvc()) {
    const bool is_mq = args[i][2] == 'Q';

    std::string_view dep_target;
    if (args[i].size() == 3) {
      // -MQ arg or -MT arg
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.dep_args.push_back(args[i]);
      state.dep_args.push_back(args[i + 1]);
      dep_target = args[i + 1];
      i++;
    } else {
      // -MQarg or -MTarg
      const std::string_view arg_view(args[i]);
      const auto arg_opt = arg_view.substr(0, 3);
      dep_target = arg_view.substr(3);
      state.dep_args.push_back(FMT("{}{}", arg_opt, dep_target));
    }

    if (args_info.dependency_target) {
      args_info.dependency_target->push_back(' ');
    } else {
      args_info.dependency_target = "";
    }
    *args_info.dependency_target +=
      is_mq ? Depfile::escape_filename(dep_target) : dep_target;

    return Statistic::none;
  }

  // MSVC -MD[d], -MT[d] and -LT[d] options are something different than GCC's
  // -MD etc.
  if (config.is_compiler_group_msvc()
      && (util::starts_with(args[i], "-MD") || util::starts_with(args[i], "-MT")
          || util::starts_with(args[i], "-LD"))) {
    // These affect compiler but also #define some things.
    state.cpp_args.push_back(args[i]);
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-fprofile-arcs") {
    args_info.profile_arcs = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-ftest-coverage") {
    args_info.generating_coverage = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-fstack-usage") {
    args_info.generating_stackusage = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  // -Zs is MSVC's -fsyntax-only equivalent
  if (args[i] == "-fsyntax-only" || args[i] == "-Zs") {
    args_info.expect_output_obj = false;
    state.compiler_only_args.push_back(args[i]);
    state.found_syntax_only = true;
    return Statistic::none;
  }

  if (args[i] == "--coverage"      // = -fprofile-arcs -ftest-coverage
      || args[i] == "-coverage") { // Undocumented but still works.
    args_info.profile_arcs = true;
    args_info.generating_coverage = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-fprofile-abs-path") {
    if (!config.sloppiness().is_enabled(core::Sloppy::gcno_cwd)) {
      // -fprofile-abs-path makes the compiler include absolute paths based on
      // the actual CWD in the .gcno file.
      state.hash_actual_cwd = true;
    }
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-fprofile-")
      || util::starts_with(args[i], "-fauto-profile")
      || args[i] == "-fbranch-probabilities") {
    if (!process_profiling_option(ctx, args_info, args[i])) {
      // The failure is logged by process_profiling_option.
      return Statistic::unsupported_compiler_option;
    }
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-fsanitize-blacklist=")) {
    args_info.sanitize_blacklists.emplace_back(args[i].substr(21));
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(args[i], "--sysroot=")) {
    auto path = std::string_view(args[i]).substr(10);
    auto relpath = Util::make_relative_path(ctx, path);
    state.common_args.push_back("--sysroot=" + relpath);
    return Statistic::none;
  }

  // Alternate form of specifying sysroot without =
  if (args[i] == "--sysroot") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    state.common_args.push_back(args[i]);
    auto relpath = Util::make_relative_path(ctx, args[i + 1]);
    state.common_args.push_back(relpath);
    i++;
    return Statistic::none;
  }

  // Alternate form of specifying target without =
  if (args[i] == "-target") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    state.common_args.push_back(args[i]);
    state.common_args.push_back(args[i + 1]);
    i++;
    return Statistic::none;
  }

  if (args[i] == "-P" || args[i] == "-Wp,-P") {
    // Avoid passing -P to the preprocessor since it removes preprocessor
    // information we need.
    state.compiler_only_args.push_back(args[i]);
    LOG("{} used; not compiling preprocessed code", args[i]);
    config.set_run_second_cpp(true);
    return Statistic::none;
  }

  if (util::starts_with(args[i], "-Wp,")) {
    if (args[i].find(",-P,") != std::string::npos
        || util::ends_with(args[i], ",-P")) {
      LOG("-P together with other preprocessor options is too hard: {}",
          args[i]);
      return Statistic::unsupported_compiler_option;
    } else if (util::starts_with(args[i], "-Wp,-MD,")
               && args[i].find(',', 8) == std::string::npos) {
      state.found_wp_md_or_mmd_opt = true;
      args_info.generating_dependencies = true;
      if (state.output_dep_origin <= OutputDepOrigin::wp) {
        state.output_dep_origin = OutputDepOrigin::wp;
        args_info.output_dep = args[i].substr(8);
      }
      state.dep_args.push_back(args[i]);
      return Statistic::none;
    } else if (util::starts_with(args[i], "-Wp,-MMD,")
               && args[i].find(',', 9) == std::string::npos) {
      state.found_wp_md_or_mmd_opt = true;
      args_info.generating_dependencies = true;
      if (state.output_dep_origin <= OutputDepOrigin::wp) {
        state.output_dep_origin = OutputDepOrigin::wp;
        args_info.output_dep = args[i].substr(9);
      }
      state.dep_args.push_back(args[i]);
      return Statistic::none;
    } else if (util::starts_with(args[i], "-Wp,-D")
               && args[i].find(',', 6) == std::string::npos) {
      // Treat it like -D.
      state.cpp_args.push_back(args[i].substr(4));
      return Statistic::none;
    } else if (args[i] == "-Wp,-MP"
               || (args[i].size() > 8 && util::starts_with(args[i], "-Wp,-M")
                   && args[i][7] == ','
                   && (args[i][6] == 'F' || args[i][6] == 'Q'
                       || args[i][6] == 'T')
                   && args[i].find(',', 8) == std::string::npos)) {
      state.dep_args.push_back(args[i]);
      return Statistic::none;
    } else if (config.direct_mode()) {
      // -Wp, can be used to pass too hard options to the preprocessor.
      // Hence, disable direct mode.
      LOG("Unsupported compiler option for direct mode: {}", args[i]);
      config.set_direct_mode(false);
    }

    // Any other -Wp,* arguments are only relevant for the preprocessor.
    state.cpp_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-MP") {
    state.dep_args.push_back(args[i]);
    return Statistic::none;
  }

  // Input charset needs to be handled specially.
  if (util::starts_with(args[i], "-finput-charset=")) {
    state.input_charset_option = args[i];
    return Statistic::none;
  }

  if (args[i] == "--serialize-diagnostics") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    args_info.generating_diagnostics = true;
    args_info.output_dia = Util::make_relative_path(ctx, args[i + 1]);
    i++;
    return Statistic::none;
  }

  if (config.compiler_type() == CompilerType::gcc) {
    if (args[i] == "-fdiagnostics-color"
        || args[i] == "-fdiagnostics-color=always") {
      state.color_diagnostics = ColorDiagnostics::always;
      state.compiler_only_args_no_hash.push_back(args[i]);
      return Statistic::none;
    } else if (args[i] == "-fno-diagnostics-color"
               || args[i] == "-fdiagnostics-color=never") {
      state.color_diagnostics = ColorDiagnostics::never;
      state.compiler_only_args_no_hash.push_back(args[i]);
      return Statistic::none;
    } else if (args[i] == "-fdiagnostics-color=auto") {
      state.color_diagnostics = ColorDiagnostics::automatic;
      state.compiler_only_args_no_hash.push_back(args[i]);
      return Statistic::none;
    }
  } else if (config.is_compiler_group_clang()) {
    // In the "-Xclang -fcolor-diagnostics" form, -Xclang is skipped and the
    // -fcolor-diagnostics argument which is passed to cc1 is handled below.
    if (args[i] == "-Xclang" && i + 1 < args.size()
        && args[i + 1] == "-fcolor-diagnostics") {
      state.compiler_only_args_no_hash.push_back(args[i]);
      ++i;
    }
    if (args[i] == "-fcolor-diagnostics") {
      state.color_diagnostics = ColorDiagnostics::always;
      state.compiler_only_args_no_hash.push_back(args[i]);
      return Statistic::none;
    } else if (args[i] == "-fno-color-diagnostics") {
      state.color_diagnostics = ColorDiagnostics::never;
      state.compiler_only_args_no_hash.push_back(args[i]);
      return Statistic::none;
    }
  }

  // GCC
  if (args[i] == "-fdirectives-only") {
    state.found_directives_only = true;
    return Statistic::none;
  }

  // Clang
  if (args[i] == "-frewrite-includes") {
    state.found_rewrite_includes = true;
    return Statistic::none;
  }

  if (args[i] == "-fno-pch-timestamp") {
    args_info.fno_pch_timestamp = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (args[i] == "-fpch-preprocess") {
    state.found_fpch_preprocess = true;
    state.common_args.push_back(args[i]);
    return Statistic::none;
  }

  if (config.sloppiness().is_enabled(core::Sloppy::clang_index_store)
      && args[i] == "-index-store-path") {
    // Xcode 9 or later calls Clang with this option. The given path includes a
    // UUID that might lead to cache misses, especially when cache is shared
    // among multiple users.
    i++;
    if (i <= args.size() - 1) {
      LOG("Skipping argument -index-store-path {}", args[i]);
    }
    return Statistic::none;
  }

  if (args[i] == "-frecord-gcc-switches") {
    state.hash_full_command_line = true;
  }

  // MSVC -u is something else than GCC -u, handle it specially.
  if (args[i] == "-u" && ctx.config.is_compiler_group_msvc()) {
    state.cpp_args.push_back(args[i]);
    return Statistic::none;
  }

  if (compopt_takes_path(args[i])) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    // In the -Xclang -include-(pch/pth) -Xclang <path> case, the path is one
    // index further behind.
    const size_t next = args[i + 1] == "-Xclang" && i + 2 < args.size() ? 2 : 1;

    if (!detect_pch(args[i],
                    args[i + next],
                    args_info.included_pch_file,
                    next == 2,
                    state)) {
      return Statistic::bad_compiler_arguments;
    }

    // Potentially rewrite path argument to relative path to get better hit
    // rate. A secondary effect is that paths in the standard error output
    // produced by the compiler will be normalized.
    std::string relpath = Util::make_relative_path(ctx, args[i + next]);
    auto& dest_args =
      compopt_affects_cpp_output(args[i]) ? state.cpp_args : state.common_args;
    dest_args.push_back(args[i]);
    if (next == 2) {
      dest_args.push_back(args[i + 1]);
    }
    dest_args.push_back(relpath);

    i += next;
    return Statistic::none;
  }

  // Detect PCH for options with concatenated path (relative or absolute).
  if (util::starts_with(args[i], "-Fp") || util::starts_with(args[i], "-Yu")) {
    const size_t path_pos = 3;
    if (!detect_pch(args[i].substr(0, path_pos),
                    args[i].substr(path_pos),
                    args_info.included_pch_file,
                    false,
                    state)) {
      return Statistic::bad_compiler_arguments;
    }

    // Fall through to the next section, so intentionally not returning here.
  }

  // Potentially rewrite concatenated absolute path argument to relative.
  if (args[i][0] == '-') {
    const auto path_pos = Util::is_absolute_path_with_prefix(args[i]);
    if (path_pos) {
      const std::string option = args[i].substr(0, *path_pos);
      if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
        const auto relpath = Util::make_relative_path(
          ctx, std::string_view(args[i]).substr(*path_pos));
        std::string new_option = option + relpath;
        if (compopt_affects_cpp_output(option)) {
          state.cpp_args.push_back(new_option);
        } else {
          state.common_args.push_back(new_option);
        }
        return Statistic::none;
      }
    }
  }

  // Options that take an argument.
  if (compopt_takes_arg(args[i])) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    if (compopt_affects_cpp_output(args[i])) {
      state.cpp_args.push_back(args[i]);
      state.cpp_args.push_back(args[i + 1]);
    } else {
      state.common_args.push_back(args[i]);
      state.common_args.push_back(args[i + 1]);
    }

    i++;
    return Statistic::none;
  }

  // Other options.
  if (args[i][0] == '-') {
    if (compopt_affects_cpp_output(args[i])
        || compopt_prefix_affects_cpp_output(args[i])) {
      state.cpp_args.push_back(args[i]);
    } else {
      state.common_args.push_back(args[i]);
    }
    return Statistic::none;
  }

  // It was not a known option.
  if (changed_from_slash) {
    args[i][0] = '/';
  }

  return std::nullopt;
}

Statistic
process_arg(const Context& ctx,
            ArgsInfo& args_info,
            Config& config,
            Args& args,
            size_t& args_index,
            ArgumentProcessingState& state)
{
  const auto processed =
    process_option_arg(ctx, args_info, config, args, args_index, state);
  if (processed) {
    const auto& error = *processed;
    return error;
  }

  size_t& i = args_index;

  // If an argument isn't a plain file then assume its an option, not an input
  // file. This allows us to cope better with unusual compiler options.
  //
  // Note that "/dev/null" is an exception that is sometimes used as an input
  // file when code is testing compiler flags.
  if (args[i] != "/dev/null") {
    auto st = Stat::stat(args[i]);
    if (!st || !st.is_regular()) {
      LOG("{} is not a regular file, not considering as input file", args[i]);
      state.common_args.push_back(args[i]);
      return Statistic::none;
    }
  }

  if (!args_info.input_file.empty()) {
    if (supported_source_extension(args[i])) {
      LOG("Multiple input files: {} and {}", args_info.input_file, args[i]);
      return Statistic::multiple_source_files;
    } else if (!state.found_c_opt && !state.found_dc_opt) {
      LOG("Called for link with {}", args[i]);
      if (args[i].find("conftest.") != std::string::npos) {
        return Statistic::autoconf_test;
      } else {
        return Statistic::called_for_link;
      }
    } else {
      LOG("Unsupported source extension: {}", args[i]);
      return Statistic::unsupported_source_language;
    }
  }

  // Rewrite to relative to increase hit rate.
  args_info.orig_input_file = args[i];
  args_info.input_file = Util::make_relative_path(ctx, args[i]);
  args_info.normalized_input_file =
    Util::normalize_concrete_absolute_path(args_info.input_file);

  return Statistic::none;
}

const char*
get_default_object_file_extension(const Config& config)
{
  return config.is_compiler_group_msvc() ? ".obj" : ".o";
}

} // namespace

ProcessArgsResult
process_args(Context& ctx)
{
  ASSERT(!ctx.orig_args.empty());

  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  // args is a copy of the original arguments given to the compiler but with
  // arguments from @file and similar constructs expanded. It's only used as a
  // temporary data structure to loop over.
  Args args = ctx.orig_args;
  ArgumentProcessingState state;

  state.common_args.push_back(args[0]); // Compiler

  std::optional<Statistic> argument_error;
  for (size_t i = 1; i < args.size(); i++) {
    const auto error =
      process_arg(ctx, ctx.args_info, ctx.config, args, i, state);
    if (error != Statistic::none && !argument_error) {
      argument_error = error;
    }
  }

  // Bail out on too hard combinations of options.
  if (state.found_mf_opt && state.found_wp_md_or_mmd_opt) {
    // GCC and Clang behave differently when "-Wp,-M[M]D,wp.d" and "-MF mf.d"
    // are used: GCC writes to wp.d but Clang writes to mf.d. We could
    // potentially support this by behaving differently depending on the
    // compiler type, but let's just bail out for now.
    LOG_RAW("-Wp,-M[M]D in combination with -MF is not supported");
    return Statistic::unsupported_compiler_option;
  }
  if (state.found_wp_md_or_mmd_opt && !args_info.output_obj.empty()
      && !state.found_md_or_mmd_opt && !args_info.dependency_target) {
    // GCC and Clang behave differently when "-Wp,-M[M]D,wp.d" is used with "-o"
    // but with neither "-MMD" nor "-MT"/"-MQ": GCC uses a dependency target
    // based on the source filename but Clang bases it on the output filename.
    // We could potentially support by behaving differently depending on the
    // compiler type, but let's just bail out for now.
    LOG_RAW("-Wp,-M[M]D with -o without -MMD, -MQ or -MT is not supported");
    return Statistic::unsupported_compiler_option;
  }

  // Don't try to second guess the compiler's heuristics for stdout handling.
  if (args_info.output_obj == "-") {
    LOG_RAW("Output file is -");
    return Statistic::output_to_stdout;
  }

  // Determine output object file.
  bool output_obj_by_source = args_info.output_obj.empty();
  if (!output_obj_by_source && ctx.config.is_compiler_group_msvc()) {
    if (*args_info.output_obj.rbegin() == '\\') {
      output_obj_by_source = true;
    } else {
      auto st = Stat::stat(args_info.output_obj);
      if (st && st.is_directory()) {
        args_info.output_obj.append("\\");
        output_obj_by_source = true;
      }
    }
  }

  if (output_obj_by_source && !args_info.input_file.empty()) {
    std::string_view extension =
      state.found_S_opt ? ".s" : get_default_object_file_extension(ctx.config);
    args_info.output_obj +=
      Util::change_extension(Util::base_name(args_info.input_file), extension);
  }

  args_info.orig_output_obj = args_info.output_obj;
  args_info.output_obj = Util::make_relative_path(ctx, args_info.output_obj);

  // Determine output dependency file.

  // On argument processing error, return now since we have determined
  // args_info.output_obj which is needed to determine the log filename in
  // CCACHE_DEBUG mode.
  if (argument_error) {
    return *argument_error;
  }

  if (state.generating_debuginfo_level_3 && !config.run_second_cpp()) {
    // Debug level 3 makes line number information incorrect when compiling
    // preprocessed code.
    LOG_RAW("Generating debug info level 3; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

#ifdef __APPLE__
  // Newer Clang versions on macOS are known to produce different debug
  // information when compiling preprocessed code.
  if (args_info.generating_debuginfo && !config.run_second_cpp()) {
    LOG_RAW("Generating debug info; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }
#endif

  if (args_info.input_file.empty()) {
    LOG_RAW("No input file found");
    return Statistic::no_input_file;
  }

  if (state.found_pch || state.found_fpch_preprocess) {
    args_info.using_precompiled_header = true;
    if (!(config.sloppiness().is_enabled(core::Sloppy::time_macros))) {
      LOG_RAW(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      LOG_RAW("Disabling direct mode");
      return Statistic::could_not_use_precompiled_header;
    }
  }

  if (args_info.profile_path.empty()) {
    args_info.profile_path = ctx.apparent_cwd;
  }

  if (!state.explicit_language.empty() && state.explicit_language == "none") {
    state.explicit_language.clear();
  }
  if (!state.explicit_language.empty()) {
    if (!language_is_supported(state.explicit_language)) {
      LOG("Unsupported language: {}", state.explicit_language);
      return Statistic::unsupported_source_language;
    }
    args_info.actual_language = state.explicit_language;
  } else {
    args_info.actual_language =
      language_for_file(args_info.input_file, config.compiler_type());
  }

  args_info.output_is_precompiled_header =
    args_info.actual_language.find("-header") != std::string::npos
    || Util::is_precompiled_header(args_info.output_obj);

  if (args_info.output_is_precompiled_header && output_obj_by_source) {
    args_info.orig_output_obj = args_info.orig_input_file + ".gch";
    args_info.output_obj =
      Util::make_relative_path(ctx, args_info.orig_output_obj);
  }

  if (args_info.output_is_precompiled_header
      && !(config.sloppiness().is_enabled(core::Sloppy::pch_defines))) {
    LOG_RAW(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    return Statistic::could_not_use_precompiled_header;
  }

  // -fsyntax-only/-Zs does not need -c
  if (!state.found_c_opt && !state.found_dc_opt && !state.found_S_opt
      && !state.found_syntax_only) {
    if (args_info.output_is_precompiled_header) {
      state.common_args.push_back("-c");
    } else {
      LOG_RAW("No -c option found");
      // Having a separate statistic for autoconf tests is useful, as they are
      // the dominant form of "called for link" in many cases.
      return args_info.input_file.find("conftest.") != std::string::npos
               ? Statistic::autoconf_test
               : Statistic::called_for_link;
    }
  }

  if (args_info.actual_language.empty()) {
    LOG("Unsupported source extension: {}", args_info.input_file);
    return Statistic::unsupported_source_language;
  }

  if (!config.run_second_cpp()
      && (args_info.actual_language == "cu"
          || args_info.actual_language == "cuda")) {
    LOG("Source language is \"{}\"; not compiling preprocessed code",
        args_info.actual_language);
    config.set_run_second_cpp(true);
  }

  args_info.direct_i_file = language_is_preprocessed(args_info.actual_language);

  if (args_info.output_is_precompiled_header && !config.run_second_cpp()) {
    // It doesn't work to create the .gch from preprocessed source.
    LOG_RAW("Creating precompiled header; not compiling preprocessed code");
    config.set_run_second_cpp(true);
  }

  if (config.cpp_extension().empty()) {
    std::string p_language = p_language_for_language(args_info.actual_language);
    config.set_cpp_extension(extension_for_language(p_language).substr(1));
  }

  if (args_info.seen_split_dwarf) {
    if (args_info.output_obj == "/dev/null") {
      // Outputting to /dev/null -> compiler won't write a .dwo, so just pretend
      // we haven't seen the -gsplit-dwarf option.
      args_info.seen_split_dwarf = false;
    } else {
      args_info.output_dwo =
        Util::change_extension(args_info.output_obj, ".dwo");
    }
  }

  // Cope with -o /dev/null.
  if (args_info.output_obj != "/dev/null") {
    auto st = Stat::stat(args_info.output_obj);
    if (st && !st.is_regular()) {
      LOG("Not a regular file: {}", args_info.output_obj);
      return Statistic::bad_output_file;
    }
  }

  auto output_dir = std::string(Util::dir_name(args_info.output_obj));
  auto st = Stat::stat(output_dir);
  if (!st || !st.is_directory()) {
    LOG("Directory does not exist: {}", output_dir);
    return Statistic::bad_output_file;
  }

  // Some options shouldn't be passed to the real compiler when it compiles
  // preprocessed code:
  //
  // -finput-charset=CHARSET (otherwise conversion happens twice)
  // -x CHARSET (otherwise the wrong language is selected)
  if (!state.input_charset_option.empty()) {
    state.cpp_args.push_back(state.input_charset_option);
  }
  if (state.found_pch && ctx.config.compiler_type() != CompilerType::msvc) {
    state.cpp_args.push_back("-fpch-preprocess");
  }
  if (!state.explicit_language.empty()) {
    state.cpp_args.push_back("-x");
    state.cpp_args.push_back(state.explicit_language);
  }

  args_info.strip_diagnostics_colors =
    state.color_diagnostics != ColorDiagnostics::automatic
      ? state.color_diagnostics == ColorDiagnostics::never
      : !color_output_possible();

  // Since output is redirected, compilers will not color their output by
  // default, so force it explicitly.
  std::optional<std::string> diagnostics_color_arg;
  if (config.is_compiler_group_clang()) {
    // Don't pass -fcolor-diagnostics when compiling assembler to avoid an
    // "argument unused during compilation" warning.
    if (args_info.actual_language != "assembler") {
      diagnostics_color_arg = "-fcolor-diagnostics";
    }
  } else if (config.compiler_type() == CompilerType::gcc) {
    diagnostics_color_arg = "-fdiagnostics-color";
  } else {
    // Other compilers shouldn't output color, so no need to strip it.
    args_info.strip_diagnostics_colors = false;
  }

  if (args_info.generating_dependencies) {
    if (state.output_dep_origin == OutputDepOrigin::none) {
      args_info.output_dep = Util::change_extension(args_info.output_obj, ".d");
      if (!config.run_second_cpp()) {
        // If we're compiling preprocessed code we're sending dep_args to the
        // preprocessor so we need to use -MF to write to the correct .d file
        // location since the preprocessor doesn't know the final object path.
        state.dep_args.push_back("-MF");
        state.dep_args.push_back(args_info.output_dep);
      }
    }

    if (!args_info.dependency_target && !config.run_second_cpp()) {
      // If we're compiling preprocessed code we're sending dep_args to the
      // preprocessor so we need to use -MQ to get the correct target object
      // file in the .d file.
      state.dep_args.push_back("-MQ");
      state.dep_args.push_back(args_info.output_obj);
    }

    if (!args_info.dependency_target) {
      args_info.dependency_target =
        Depfile::escape_filename(args_info.orig_output_obj);
    }
  }

  if (args_info.generating_stackusage) {
    auto default_sufile_name =
      Util::change_extension(args_info.output_obj, ".su");
    args_info.output_su = Util::make_relative_path(ctx, default_sufile_name);
  }

  Args compiler_args = state.common_args;
  compiler_args.push_back(state.compiler_only_args_no_hash);
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

  if (state.found_xarch_arch && !args_info.arch_args.empty()) {
    if (args_info.arch_args.size() > 1) {
      LOG_RAW(
        "Multiple -arch options in combination with -Xarch_* not supported");
      return Statistic::unsupported_compiler_option;
    } else if (args_info.arch_args[0] != *state.found_xarch_arch) {
      LOG_RAW("-arch option not matching -Xarch_* option not supported");
      return Statistic::unsupported_compiler_option;
    }
  }

  for (const auto& arch : args_info.arch_args) {
    compiler_args.push_back("-arch");
    compiler_args.push_back(arch);
  }

  Args preprocessor_args = state.common_args;
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

  Args extra_args_to_hash = state.compiler_only_args;
  if (config.run_second_cpp()) {
    extra_args_to_hash.push_back(state.dep_args);
  }
  if (state.hash_full_command_line) {
    extra_args_to_hash.push_back(ctx.orig_args);
  }

  if (diagnostics_color_arg) {
    compiler_args.push_back(*diagnostics_color_arg);
    if (!config.run_second_cpp()) {
      // If we're compiling preprocessed code we're keeping any warnings from
      // the preprocessor, so we need to make sure that they are in color.
      preprocessor_args.push_back(*diagnostics_color_arg);
    }
    if (ctx.config.depend_mode()) {
      // The compiler is invoked with the original arguments in the depend mode.
      ctx.args_info.depend_extra_args.push_back(*diagnostics_color_arg);
    }
  }

  return {
    preprocessor_args,
    extra_args_to_hash,
    compiler_args,
    state.hash_actual_cwd,
  };
}
