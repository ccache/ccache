// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include <ccache/argsinfo.hpp>
#include <ccache/compopt.hpp>
#include <ccache/context.hpp>
#include <ccache/core/common.hpp>
#include <ccache/depfile.hpp>
#include <ccache/language.hpp>
#include <ccache/util/args.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/tokenizer.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = util::filesystem;

using core::Statistic;
using util::DirEntry;

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

class ArgumentProcessingState
{
public:
  std::optional<std::string> found_c_opt;
  std::optional<std::string> found_dc_opt;
  bool found_S_opt = false;
  bool found_analyze_opt = false;
  bool found_pch = false;
  bool found_fpch_preprocess = false;
  bool found_Yu = false;
  bool found_Yc = false;
  fs::path found_Fp_file;
  bool found_valid_Fp = false;
  bool found_syntax_only = false;
  ColorDiagnostics color_diagnostics = ColorDiagnostics::automatic;
  std::unordered_map<std::string, std::vector<std::string>> xarch_args;
  bool found_mf_opt = false;
  bool found_wp_md_or_mmd_opt = false;
  bool found_md_or_mmd_opt = false;
  bool found_Wa_a_opt = false;

  std::string explicit_language;             // As specified with -x.
  std::string input_charset_option;          // -finput-charset=...
  std::string last_seen_msvc_z_debug_option; // /Z7, /Zi or /ZI

  // Is the dependency file set via -Wp,-M[M]D,target or -MFtarget?
  OutputDepOrigin output_dep_origin = OutputDepOrigin::none;

  // Arguments classified as input files.
  std::vector<fs::path> input_files;

  // Whether to include the full command line in the hash.
  bool hash_full_command_line = false;

  // Whether to include the actual CWD in the hash.
  bool hash_actual_cwd = false;

  template<typename T>
  void
  add_common_arg(T&& arg)
  {
    m_preprocessor_args.push_back(std::forward<T>(arg));
    m_compiler_args.push_back(std::forward<T>(arg));
  }

  template<typename T>
  void
  add_compiler_only_arg(T&& arg)
  {
    m_compiler_args.push_back(std::forward<T>(arg));
    m_extra_args_to_hash.push_back(std::forward<T>(arg));
  }

  template<typename T>
  void
  add_compiler_only_arg_no_hash(T&& arg)
  {
    m_compiler_args.push_back(std::forward<T>(arg));
  }

  template<typename T>
  void
  add_extra_args_to_hash(T&& args)
  {
    m_extra_args_to_hash.push_back(std::forward<T>(args));
  }

  template<typename T>
  void
  add_native_arg(T&& arg)
  {
    m_native_args.push_back(std::forward<T>(arg));
  }

  ProcessArgsResult
  to_result()
  {
    return {
      m_preprocessor_args,
      m_compiler_args,
      m_extra_args_to_hash,
      m_native_args,
      hash_actual_cwd,
    };
  }

private:
  util::Args m_preprocessor_args;
  util::Args m_compiler_args;
  util::Args m_extra_args_to_hash;
  util::Args m_native_args;
};

bool
color_output_possible()
{
  const char* term_env = getenv("TERM");
  return isatty(STDERR_FILENO) && term_env
         && util::to_lowercase(term_env) != "dumb";
}

bool
detect_pch(const std::string& option,
           const std::string& arg,
           ArgsInfo& args_info,
           bool is_cc1_option,
           ArgumentProcessingState& state)
{
  auto& included_pch_file = args_info.included_pch_file;

  // Try to be smart about detecting precompiled headers.
  // If the option is an option for Clang (is_cc1_option), don't accept
  // anything just because it has a corresponding precompiled header,
  // because Clang doesn't behave that way either.
  fs::path pch_file;
  if (option == "-Yc") {
    state.found_Yc = true;
    args_info.generating_pch = true;
    if (!state.found_Fp_file.empty()) {
      included_pch_file = state.found_Fp_file;
      return true;
    }
  }
  if (option == "-Yu") {
    state.found_Yu = true;
    if (state.found_valid_Fp) { // Use file set by -Fp.
      LOG("Detected use of precompiled header: {}", included_pch_file);
      pch_file = included_pch_file;
      included_pch_file.clear(); // reset pch file set from /Fp
    } else {
      fs::path file = util::with_extension(arg, ".pch");
      if (fs::is_regular_file(file)) {
        LOG("Detected use of precompiled header: {}", file);
        pch_file = file;
      }
    }
  } else if (option == "-Fp") {
    args_info.orig_included_pch_file = arg;
    std::string file = arg;
    if (!fs::path(file).has_extension()) {
      file += ".pch";
    }

    state.found_Fp_file = file;

    if (state.found_Yc) {
      included_pch_file = state.found_Fp_file;
      return true;
    }
    if (DirEntry(file).is_regular_file()) {
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
    if (DirEntry(arg).is_regular_file()) {
      LOG("Detected use of precompiled header: {}", arg);
      pch_file = arg;
    }
  } else if (!is_cc1_option) {
    for (const auto& extension : {".gch", ".pch", ".pth"}) {
      std::string path = arg + extension;
      DirEntry de(path);
      if (de.is_regular_file() || de.is_directory()) {
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
                         std::string_view arg)
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

  if (util::starts_with(arg, "-fprofile-update")) {
    return true;
  }

  if (util::starts_with(arg, "-fprofile-prefix-path=")) {
    args_info.profile_prefix_path = arg.substr(arg.find('=') + 1);
    LOG("Set profile prefix path to {}", args_info.profile_prefix_path);
    return true;
  }

  fs::path new_profile_path;
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

std::string
make_dash_option(const Config& config, const std::string& arg)
{
  std::string new_arg = arg;
  if (config.is_compiler_group_msvc() && util::starts_with(arg, "/")) {
    // MSVC understands both /option and -option, so convert all /option to
    // -option to simplify our handling.
    new_arg[0] = '-';
  }
  return new_arg;
}

bool
is_msvc_z_debug_option(std::string_view arg)
{
  static const char* debug_options[] = {"-Z7", "-ZI", "-Zi"};
  return std::find(std::begin(debug_options), std::end(debug_options), arg)
         != std::end(debug_options);
}

// Returns std::nullopt if the option wasn't recognized, otherwise the error
// code (with Statistic::none for "no error").
std::optional<Statistic>
process_option_arg(const Context& ctx,
                   ArgsInfo& args_info,
                   Config& config,
                   util::Args& args,
                   size_t& args_index,
                   ArgumentProcessingState& state)
{
  size_t& i = args_index;

  if (option_should_be_ignored(args[i], ctx.ignore_options())) {
    LOG("Not processing ignored option: {}", args[i]);
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (args[i] == "--ccache-skip") {
    i++;
    if (i == args.size()) {
      LOG_RAW("--ccache-skip lacks an argument");
      return Statistic::bad_compiler_arguments;
    }
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  // arg should only be used when detecting options. It should not be added to
  // state.*_args since it's potentially != args[i].
  std::string arg = make_dash_option(ctx.config, args[i]);

  // Exit early if we notice a non-option argument right away.
  if (arg.empty() || (arg[0] != '-' && arg[0] != '@')) {
    return std::nullopt;
  }

  if (arg == "-ivfsoverlay"
      && !(config.sloppiness().contains(core::Sloppy::ivfsoverlay))) {
    LOG_RAW(
      "You have to specify \"ivfsoverlay\" sloppiness when using"
      " -ivfsoverlay to get hits");
    ++i;
    return Statistic::unsupported_compiler_option;
  }

  // Special case for -E.
  if (arg == "-E") {
    return Statistic::called_for_preprocessing;
  }
  // MSVC -P is -E with output to a file.
  if (arg == "-P" && ctx.config.is_compiler_group_msvc()) {
    return Statistic::called_for_preprocessing;
  }

  // Handle "@file" argument.
  if (util::starts_with(arg, "@") || util::starts_with(arg, "-@")) {
    const char* argpath = arg.c_str() + 1;

    if (argpath[-1] == '-') {
      ++argpath;
    }
    auto file_args =
      util::Args::from_response_file(argpath, config.response_file_format());
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
      && (arg == "-optf" || arg == "--options-file")) {
    if (i == args.size() - 1) {
      LOG("Expected argument after {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    ++i;

    // Argument is a comma-separated list of files.
    auto paths = util::split_into_strings(args[i], ",");
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
      auto file_args = util::Args::from_response_file(
        *it, util::Args::ResponseFileFormat::posix);
      if (!file_args) {
        LOG("Couldn't read CUDA options file {}", *it);
        return Statistic::bad_compiler_arguments;
      }

      args.insert(i + 1, *file_args);
    }

    return Statistic::none;
  }

  if (arg == "-fdump-ipa-clones") {
    args_info.generating_ipa_clones = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  // These are always too hard.
  if (compopt_too_hard(arg) || util::starts_with(arg, "-fdump-")
      || util::starts_with(arg, "-MJ")
      || util::starts_with(arg, "--config-system-dir=")
      || util::starts_with(arg, "--config-user-dir=")) {
    LOG("Compiler option {} is unsupported", args[i]);
    return Statistic::unsupported_compiler_option;
  }

  // These are too hard in direct mode.
  if (config.direct_mode() && compopt_too_hard_for_direct_mode(arg)) {
    LOG("Unsupported compiler option for direct mode: {}", args[i]);
    config.set_direct_mode(false);
  }

  // Handle -Xpreprocessor options.
  if (util::starts_with(arg, "-Xpreprocessor")) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    if (args[i + 1] != "-fopenmp") {
      LOG("Unsupported compiler option for direct mode: {} {}",
          args[i],
          args[i + 1]);
      config.set_direct_mode(false);
    }
  }

  // Handle -Xarch_* options.
  if (util::starts_with(arg, "-Xarch_")) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    const auto arch = arg.substr(7);
    auto it = state.xarch_args.emplace(arch, std::vector<std::string>()).first;
    it->second.emplace_back(args[i + 1]);
    if (arch == "host" || arch == "device") {
      state.add_common_arg(args[i]);
      state.add_common_arg(args[i + 1]);
    }
    ++i;
    return Statistic::none;
  }

  // Handle -arch options.
  if (arg == "-arch") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    ++i;
    args_info.arch_args.emplace_back(args[i]);
    return Statistic::none;
  }

  // Some arguments that clang passes directly to cc1 (related to precompiled
  // headers) need the usual ccache handling. In those cases, the -Xclang
  // prefix is skipped and the cc1 argument is handled instead.
  if (arg == "-Xclang" && i + 1 < args.size()
      && (args[i + 1] == "-emit-pch" || args[i + 1] == "-emit-pth"
          || args[i + 1] == "-include-pch" || args[i + 1] == "-include-pth"
          || args[i + 1] == "-include" || args[i + 1] == "--include"
          || args[i + 1] == "-fno-pch-timestamp")) {
    if (compopt_affects_compiler_output(args[i + 1])) {
      state.add_compiler_only_arg(args[i]);
    } else {
      state.add_common_arg(args[i]);
    }
    ++i;
    arg = make_dash_option(ctx.config, args[i]);
  }

  if (util::starts_with(arg, "-Wa,")) {
    for (const auto part : util::Tokenizer(&arg[4], ",")) {
      if (util::starts_with(part, "-a")) {
        if (state.found_Wa_a_opt) {
          LOG_RAW(
            "Multiple assembler listing options (-Wa,-a) are not supported");
          return Statistic::unsupported_compiler_option;
        }
        state.found_Wa_a_opt = true;

        const auto eq_pos = part.find('=');
        if (eq_pos != std::string_view::npos) {
          args_info.output_al = part.substr(eq_pos + 1);
        }
      }
    }
  }

  // Handle options that should not be passed to the preprocessor.
  if (compopt_affects_compiler_output(arg)
      || (i + 1 < args.size() && arg == "-Xclang"
          && compopt_affects_compiler_output(args[i + 1]))) {
    if (i + 1 < args.size() && arg == "-Xclang") {
      state.add_compiler_only_arg(args[i]);
      ++i;
      arg = make_dash_option(ctx.config, args[i]);
    }
    state.add_compiler_only_arg(args[i]);
    // Note: "-Xclang -option-that-takes-arg -Xclang arg" is not handled below
    // yet.
    if (compopt_takes_arg(arg)
        || (config.compiler_type() == CompilerType::nvcc && arg == "-Werror")) {
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.add_compiler_only_arg(args[i + 1]);
      ++i;
    }
    return Statistic::none;
  }
  if (compopt_prefix_affects_compiler_output(arg)
      || (i + 1 < args.size() && arg == "-Xclang"
          && compopt_prefix_affects_compiler_output(args[i + 1]))) {
    if (i + 1 < args.size() && arg == "-Xclang") {
      state.add_compiler_only_arg(args[i]);
      ++i;
    }
    state.add_compiler_only_arg(args[i]);
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
  if (arg == "-fmodules") {
    if (!config.depend_mode() || !config.direct_mode()) {
      LOG("Compiler option {} is unsupported without direct depend mode",
          args[i]);
      return Statistic::could_not_use_modules;
    } else if (!(config.sloppiness().contains(core::Sloppy::modules))) {
      LOG_RAW(
        "You have to specify \"modules\" sloppiness when using"
        " -fmodules to get hits");
      return Statistic::could_not_use_modules;
    }
  }

  if (arg == "-c" || arg == "--compile") { // --compile is NVCC
    state.found_c_opt = args[i];
    return Statistic::none;
  }

  if (config.is_compiler_group_msvc()) {
    // MSVC /Fo with no space.
    if (util::starts_with(arg, "-Fo")) {
      args_info.output_obj = arg.substr(3);
      return Statistic::none;
    }

    // MSVC /Tc and /Tp options in concatenated form for specifying input file.
    if (arg.length() > 3 && util::starts_with(arg, "-T")
        && (arg[2] == 'c' || arg[2] == 'p')) {
      args_info.input_file_prefix = arg.substr(0, 3);
      state.input_files.emplace_back(arg.substr(3));
      return Statistic::none;
    }

    if (arg == "-TC") {
      args_info.actual_language = "c";
      state.add_common_arg(args[i]);
      return Statistic::none;
    }

    if (arg == "-TP") {
      args_info.actual_language = "c++";
      state.add_common_arg(args[i]);
      return Statistic::none;
    }
  }

  // -dc implies -c when using NVCC with separable compilation.
  if ((arg == "-dc" || arg == "--device-c")
      && config.compiler_type() == CompilerType::nvcc) {
    state.found_dc_opt = args[i];
    return Statistic::none;
  }

  // -S changes the default extension.
  if (arg == "-S") {
    state.add_common_arg(args[i]);
    state.found_S_opt = true;
    return Statistic::none;
  }

  // --analyze changes the default extension too
  if (arg == "--analyze") {
    state.add_common_arg(args[i]);
    state.found_analyze_opt = true;
    return Statistic::none;
  }

  if (util::starts_with(arg, "-x")) {
    if (arg.length() >= 3 && !util::is_lower(arg[2])) {
      // -xCODE (where CODE can be e.g. Host or CORE-AVX2, always starting with
      // an uppercase letter) is an ordinary Intel compiler option, not a
      // language specification. (GCC's "-x" language argument is always
      // lowercase.)
      state.add_common_arg(args[i]);
      return Statistic::none;
    }

    // Special handling for -x: remember the last specified language before the
    // input file and strip all -x options from the arguments.
    if (arg.length() == 2) {
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      if (state.input_files.empty()) {
        state.explicit_language = args[i + 1];
      }
      i++;
      return Statistic::none;
    }

    DEBUG_ASSERT(arg.length() >= 3);
    if (state.input_files.empty()) {
      state.explicit_language = arg.substr(2);
    }
    return Statistic::none;
  }

  // We need to work out where the output was meant to go.
  if (arg == "-o") {
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
  if (util::starts_with(arg, "-o")
      && config.compiler_type() != CompilerType::nvcc
      && config.compiler_type() != CompilerType::msvc) {
    args_info.output_obj = arg.substr(2);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fdebug-prefix-map=")
      || util::starts_with(arg, "-ffile-prefix-map=")) {
    std::string map = arg.substr(arg.find('=') + 1);
    args_info.debug_prefix_maps.push_back(map);
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fcoverage-prefix-map=")) {
    std::string map = arg.substr(arg.find('=') + 1);
    args_info.coverage_prefix_maps.push_back(map);
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fdebug-compilation-dir")
      || util::starts_with(arg, "-ffile-compilation-dir")) {
    std::string compilation_dir;
    // -ffile-compilation-dir cannot be followed by a space.
    if (arg == "-fdebug-compilation-dir") {
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.add_common_arg(args[i]);
      compilation_dir = args[i + 1];
      i++;
    } else {
      const auto eq_pos = arg.find('=');
      if (eq_pos != std::string_view::npos) {
        compilation_dir = arg.substr(eq_pos + 1);
      }
    }
    args_info.compilation_dir = std::move(compilation_dir);
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (std::string_view prefix{"-fcoverage-compilation-dir="};
      util::starts_with(arg, prefix)) {
    args_info.coverage_compilation_dir = arg.substr(prefix.length());
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  // Debugging is handled specially, so that we know if we can strip line
  // number info.
  if (util::starts_with(arg, "-g")) {
    state.add_common_arg(args[i]);

    if (util::starts_with(arg, "-gdwarf")) {
      // Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
      // debug info on level 2.
      args_info.generating_debuginfo = true;
      return Statistic::none;
    }

    if (util::starts_with(arg, "-gz")) {
      // -gz[=type] neither disables nor enables debug info.
      return Statistic::none;
    }

    char last_char = arg.back();
    if (last_char == '0') {
      // "-g0", "-ggdb0" or similar: All debug information disabled.
      args_info.generating_debuginfo = false;
    } else {
      args_info.generating_debuginfo = true;
      if (arg == "-gsplit-dwarf") {
        args_info.seen_split_dwarf = true;
      }
    }
    return Statistic::none;
  }

  if (config.is_compiler_group_msvc() && !config.is_compiler_group_clang()
      && is_msvc_z_debug_option(arg)) {
    state.last_seen_msvc_z_debug_option = args[i];
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (config.is_compiler_group_msvc() && util::starts_with(arg, "-Fd")) {
    state.add_compiler_only_arg_no_hash(args[i]);
    return Statistic::none;
  }

  if (config.is_compiler_group_msvc()
      && (util::starts_with(arg, "-MP") || arg == "-FS")) {
    state.add_compiler_only_arg_no_hash(args[i]);
    return Statistic::none;
  }

  // These options require special handling, because they behave differently
  // with gcc -E, when the output file is not specified.
  if (!config.is_compiler_group_msvc()
      && (arg == "-MD"
          || arg == "-MMD"
          // nvcc -MD:
          || arg == "--generate-dependencies-with-compile"
          // nvcc -MMD:
          || arg == "--generate-nonsystem-dependencies-with-compile")) {
    state.found_md_or_mmd_opt = true;
    args_info.generating_dependencies = true;
    state.add_compiler_only_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-MF")
      // nvcc -MF:
      || arg == "--dependency-output") {
    state.found_mf_opt = true;

    std::string dep_file;
    bool separate_argument = (arg.size() == 3 || arg == "--dependency-output");
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
      dep_file = arg.substr(arg[3] == '=' ? 4 : 3);
    }

    if (state.output_dep_origin <= OutputDepOrigin::mf) {
      state.output_dep_origin = OutputDepOrigin::mf;
      args_info.output_dep = core::make_relative_path(ctx, dep_file);
    }
    // Keep the format of the args the same.
    if (separate_argument) {
      state.add_compiler_only_arg("-MF");
      state.add_compiler_only_arg(args_info.output_dep);
    } else {
      state.add_compiler_only_arg(FMT("-MF{}", args_info.output_dep));
    }
    return Statistic::none;
  }

  if (!config.is_compiler_group_msvc()
      && (util::starts_with(arg, "-MQ")
          || util::starts_with(arg, "-MT")
          // nvcc -MT:
          || arg == "--dependency-target-name")) {
    const bool is_mq = arg[2] == 'Q';

    std::string_view dep_target;
    if (arg.size() == 3 || arg == "--dependency-target-name") {
      // -MQ arg or -MT arg
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.add_compiler_only_arg(args[i]);
      state.add_compiler_only_arg(args[i + 1]);
      dep_target = args[i + 1];
      i++;
    } else {
      // -MQarg or -MTarg
      const std::string_view arg_view(arg);
      const auto arg_opt = arg_view.substr(0, 3);
      dep_target = arg_view.substr(3);
      state.add_compiler_only_arg(FMT("{}{}", arg_opt, dep_target));
    }

    if (args_info.dependency_target) {
      args_info.dependency_target->push_back(' ');
    } else {
      args_info.dependency_target = "";
    }
    *args_info.dependency_target +=
      is_mq ? depfile::escape_filename(dep_target) : dep_target;

    return Statistic::none;
  }

  // MSVC -MD[d], -MT[d] and -LT[d] options are something different than GCC's
  // -MD etc.
  if (config.is_compiler_group_msvc()
      && (util::starts_with(arg, "-MD") || util::starts_with(arg, "-MT")
          || util::starts_with(arg, "-LD"))) {
    // These affect compiler but also #define some things.
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-showIncludes"
      // clang-cl:
      || arg == "-showIncludes:user") {
    args_info.generating_includes = true;
    state.add_compiler_only_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-fprofile-arcs") {
    args_info.profile_arcs = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-ftest-coverage") {
    args_info.generating_coverage = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-fstack-usage") {
    args_info.generating_stackusage = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  // This covers all the different marker cases
  if (util::starts_with(arg, "-fcallgraph-info")) {
    args_info.generating_callgraphinfo = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  // -Zs is MSVC's -fsyntax-only equivalent
  if (arg == "-fsyntax-only" || arg == "-Zs") {
    args_info.expect_output_obj = false;
    state.add_compiler_only_arg(args[i]);
    state.found_syntax_only = true;
    return Statistic::none;
  }

  if (arg == "--coverage"      // = -fprofile-arcs -ftest-coverage
      || arg == "-coverage") { // Undocumented but still works.
    args_info.profile_arcs = true;
    args_info.generating_coverage = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-fprofile-abs-path") {
    if (!config.sloppiness().contains(core::Sloppy::gcno_cwd)) {
      // -fprofile-abs-path makes the compiler include absolute paths based on
      // the actual CWD in the .gcno file.
      state.hash_actual_cwd = true;
    }
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fprofile-")
      || util::starts_with(arg, "-fauto-profile")
      || arg == "-fbranch-probabilities") {
    if (!process_profiling_option(ctx, args_info, arg)) {
      // The failure is logged by process_profiling_option.
      return Statistic::unsupported_compiler_option;
    }
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fsanitize-blacklist=")) {
    auto path = std::string_view(args[i]).substr(21);
    args_info.sanitize_blacklists.emplace_back(path);
    auto relpath = core::make_relative_path(ctx, path);
    state.add_common_arg(FMT("-fsanitize-blacklist={}", relpath));
    return Statistic::none;
  }

  if (util::starts_with(arg, "--sysroot=")) {
    auto path = std::string_view(arg).substr(10);
    auto relpath = core::make_relative_path(ctx, path);
    state.add_common_arg(FMT("--sysroot={}", relpath));
    return Statistic::none;
  }

  // Alternate form of specifying sysroot without =
  if (arg == "--sysroot") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    state.add_common_arg(args[i]);
    auto relpath = core::make_relative_path(ctx, args[i + 1]);
    state.add_common_arg(relpath);
    i++;
    return Statistic::none;
  }

  // Alternate form of specifying target without =
  if (arg == "-target") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    state.add_common_arg(args[i]);
    state.add_common_arg(args[i + 1]);
    i++;
    return Statistic::none;
  }

  if (arg == "-P" || arg == "-Wp,-P") {
    // Avoid passing -P to the preprocessor since it removes preprocessor
    // information we need.
    state.add_compiler_only_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-Wp,")) {
    if (arg.find(",-P,") != std::string::npos || util::ends_with(arg, ",-P")) {
      LOG("-P together with other preprocessor options is too hard: {}",
          args[i]);
      return Statistic::unsupported_compiler_option;
    } else if (util::starts_with(arg, "-Wp,-MD,")
               && arg.find(',', 8) == std::string::npos) {
      state.found_wp_md_or_mmd_opt = true;
      args_info.generating_dependencies = true;
      if (state.output_dep_origin <= OutputDepOrigin::wp) {
        state.output_dep_origin = OutputDepOrigin::wp;
        args_info.output_dep = arg.substr(8);
      }
      state.add_compiler_only_arg(args[i]);
      return Statistic::none;
    } else if (util::starts_with(arg, "-Wp,-MMD,")
               && arg.find(',', 9) == std::string::npos) {
      state.found_wp_md_or_mmd_opt = true;
      args_info.generating_dependencies = true;
      if (state.output_dep_origin <= OutputDepOrigin::wp) {
        state.output_dep_origin = OutputDepOrigin::wp;
        args_info.output_dep = arg.substr(9);
      }
      state.add_compiler_only_arg(args[i]);
      return Statistic::none;
    } else if ((util::starts_with(arg, "-Wp,-D")
                || util::starts_with(arg, "-Wp,-U"))
               && arg.find(',', 6) == std::string::npos) {
      state.add_common_arg(args[i]);
      return Statistic::none;
    } else if (arg == "-Wp,-MP"
               || (arg.size() > 8 && util::starts_with(arg, "-Wp,-M")
                   && arg[7] == ','
                   && (arg[6] == 'F' || arg[6] == 'Q' || arg[6] == 'T')
                   && arg.find(',', 8) == std::string::npos)) {
      state.add_compiler_only_arg(args[i]);
      return Statistic::none;
    } else if (config.direct_mode()) {
      // -Wp, can be used to pass too hard options to the preprocessor.
      // Hence, disable direct mode.
      LOG("Unsupported compiler option for direct mode: {}", args[i]);
      config.set_direct_mode(false);
    }

    // Any other -Wp,* arguments are only relevant for the preprocessor.
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-MP"
      // nvcc -MP:
      || arg == "--generate-dependency-targets") {
    state.add_compiler_only_arg(args[i]);
    return Statistic::none;
  }

  // Input charset needs to be handled specially.
  if (util::starts_with(arg, "-finput-charset=")) {
    state.input_charset_option = args[i];
    return Statistic::none;
  }

  if (arg == "--serialize-diagnostics") {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }
    state.add_compiler_only_arg(args[i]);
    state.add_compiler_only_arg_no_hash(args[i + 1]);
    args_info.output_dia = args[i + 1];
    i++;
    return Statistic::none;
  }

  const std::string_view source_dep_directives_opt =
    "-sourceDependencies:directives";
  if (util::starts_with(arg, source_dep_directives_opt)) {
    LOG("Compiler option {} is unsupported", args[i]);
    return Statistic::unsupported_compiler_option;
  }

  const std::string_view source_dep_opt = "-sourceDependencies";
  if (util::starts_with(arg, source_dep_opt)) {
    // The generated file embeds absolute include paths resolved relative to the
    // actual working directory even when -I uses relative paths. To avoid false
    // positive cache hits across different working directories, bind the result
    // key to the actual CWD.
    //
    // Note: A future alternative could be to instead disable direct/depend mode
    // and let the preprocessor create the file instead.
    LOG("Hashing current working directory since {} is used", arg);
    state.hash_actual_cwd = true;

    state.add_compiler_only_arg(args[i]);

    if (arg == source_dep_opt) {
      // /sourceDependencies FILE
      if (i == args.size() - 1) {
        LOG("Missing argument to {}", args[i]);
        return Statistic::bad_compiler_arguments;
      }
      state.add_compiler_only_arg_no_hash(args[i + 1]);
      args_info.output_sd = args[i + 1];
      ++i;
    } else {
      // /sourceDependenciesFILE
      auto file = std::string_view(args[i]).substr(source_dep_opt.length());
      if (file == "-") {
        LOG("Compiler option {} is unsupported", args[i]);
        return Statistic::unsupported_compiler_option;
      }
      if (fs::is_directory(file)) {
        LOG("{} with directory ({}) is unsupported", args[i], file);
        return Statistic::unsupported_compiler_option;
      }
      args_info.output_sd = file;
    }
    return Statistic::none;
  }

  if (config.compiler_type() == CompilerType::gcc) {
    if (arg == "-fdiagnostics-color" || arg == "-fdiagnostics-color=always") {
      state.color_diagnostics = ColorDiagnostics::always;
      state.add_compiler_only_arg_no_hash(args[i]);
      return Statistic::none;
    } else if (arg == "-fno-diagnostics-color"
               || arg == "-fdiagnostics-color=never") {
      state.color_diagnostics = ColorDiagnostics::never;
      state.add_compiler_only_arg_no_hash(args[i]);
      return Statistic::none;
    } else if (arg == "-fdiagnostics-color=auto") {
      state.color_diagnostics = ColorDiagnostics::automatic;
      state.add_compiler_only_arg_no_hash(args[i]);
      return Statistic::none;
    }
  } else if (config.is_compiler_group_clang()) {
    // In the "-Xclang -fcolor-diagnostics" form, -Xclang is skipped and the
    // -fcolor-diagnostics argument which is passed to cc1 is handled below.
    if (arg == "-Xclang" && i + 1 < args.size()
        && args[i + 1] == "-fcolor-diagnostics") {
      state.add_compiler_only_arg_no_hash(args[i]);
      ++i;
      arg = make_dash_option(ctx.config, args[i]);
    }
    if (arg == "-fdiagnostics-color" || arg == "-fdiagnostics-color=always"
        || arg == "-fcolor-diagnostics") {
      state.color_diagnostics = ColorDiagnostics::always;
      state.add_compiler_only_arg_no_hash(args[i]);
      return Statistic::none;
    } else if (arg == "-fno-diagnostics-color"
               || arg == "-fdiagnostics-color=never"
               || arg == "-fno-color-diagnostics") {
      state.color_diagnostics = ColorDiagnostics::never;
      state.add_compiler_only_arg_no_hash(args[i]);
      return Statistic::none;
    }
  }

  if (arg == "-fno-pch-timestamp") {
    args_info.fno_pch_timestamp = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (arg == "-fpch-preprocess") {
    state.found_fpch_preprocess = true;
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (util::starts_with(arg, "-fbuild-session-file")
      && !(config.sloppiness().contains(core::Sloppy::time_macros))) {
    args_info.build_session_file = arg.substr(arg.find('=') + 1);
  }

  if (config.sloppiness().contains(core::Sloppy::clang_index_store)
      && arg == "-index-store-path") {
    // Xcode 9 or later calls Clang with this option. The given path includes a
    // UUID that might lead to cache misses, especially when cache is shared
    // among multiple users.
    i++;
    if (i <= args.size() - 1) {
      LOG("Skipping argument -index-store-path {}", args[i]);
    }
    return Statistic::none;
  }

  if (arg == "-frecord-gcc-switches") {
    state.hash_full_command_line = true;
    LOG_RAW(
      "Found -frecord-gcc-switches, hashing original command line unmodified");
  }

  // -march=native, -mcpu=native and -mtune=native make the compiler optimize
  // differently depending on platform.
  if (arg == "-march=native" || arg == "-mcpu=native"
      || arg == "-mtune=native") {
    LOG("Detected system dependent argument: {}", args[i]);
    state.add_native_arg(args[i]);
  }

  // MSVC -u is something else than GCC -u, handle it specially.
  if (arg == "-u" && ctx.config.is_compiler_group_msvc()) {
    state.add_common_arg(args[i]);
    return Statistic::none;
  }

  if (compopt_takes_arg(arg) && compopt_takes_path(arg)) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    // In the -Xclang -include-(pch/pth) -Xclang <path> case, the path is one
    // index further behind.
    const size_t next = args[i + 1] == "-Xclang" && i + 2 < args.size() ? 2 : 1;

    if (!detect_pch(arg, args[i + next], args_info, next == 2, state)) {
      return Statistic::bad_compiler_arguments;
    }

    // Potentially rewrite path argument to relative path to get better hit
    // rate. A secondary effect is that paths in the standard error output
    // produced by the compiler will be normalized.
    fs::path relpath = core::make_relative_path(ctx, args[i + next]);
    state.add_common_arg(args[i]);
    if (next == 2) {
      state.add_common_arg(args[i + 1]);
    }
    state.add_common_arg(relpath);

    i += next;
    return Statistic::none;
  }

  // Detect PCH for options with concatenated path (relative or absolute).
  if (util::starts_with(arg, "-include") || util::starts_with(arg, "-Fp")
      || util::starts_with(arg, "-Yu") || util::starts_with(arg, "-Yc")) {
    const size_t path_pos = util::starts_with(arg, "-include") ? 8 : 3;
    if (!detect_pch(arg.substr(0, path_pos),
                    arg.substr(path_pos),
                    args_info,
                    false,
                    state)) {
      return Statistic::bad_compiler_arguments;
    }
    // Fall through to the next section, so intentionally not returning here.
  }

  // Potentially rewrite concatenated absolute path argument to relative.
  if (arg[0] == '-') {
    const auto [option, path] = util::split_option_with_concat_path(arg);
    if (path && compopt_takes_concat_arg(option)
        && compopt_takes_path(option)) {
      const auto relpath = core::make_relative_path(ctx, *path);
      std::string new_option = FMT("{}{}", option, relpath);
      if (compopt_affects_cpp_output(option)) {
        state.add_common_arg(std::move(new_option));
      } else {
        state.add_common_arg(std::move(new_option));
      }
      return Statistic::none;
    }
  }

  // Options that take an argument.
  if (compopt_takes_arg(arg)) {
    if (i == args.size() - 1) {
      LOG("Missing argument to {}", args[i]);
      return Statistic::bad_compiler_arguments;
    }

    if (compopt_affects_cpp_output(arg)) {
      state.add_common_arg(args[i]);
      state.add_common_arg(args[i + 1]);
    } else {
      state.add_common_arg(args[i]);
      state.add_common_arg(args[i + 1]);
    }

    i++;
    return Statistic::none;
  }

  if (args[i] == "--") {
    args_info.seen_double_dash = true;
    return Statistic::none;
  }

  // Other options.
  if (arg[0] == '-') {
    if (compopt_affects_cpp_output(arg)
        || compopt_prefix_affects_cpp_output(arg)) {
      state.add_common_arg(args[i]);
      return Statistic::none;
    } else if (ctx.config.is_compiler_group_msvc()
               && args[i][0] == '/' // Intentionally not checking arg here
               && DirEntry(args[i]).is_regular_file()) {
      // Likely the input file, which is handled in process_arg later.
    } else {
      state.add_common_arg(args[i]);
      return Statistic::none;
    }
  }

  // It was not a known option.
  return std::nullopt;
}

Statistic
process_arg(const Context& ctx,
            ArgsInfo& args_info,
            Config& config,
            util::Args& args,
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

  // If an argument isn't a plain file then assume it's an option, not an input
  // file. This allows us to cope better with unusual compiler options.
  //
  // Note that "/dev/null" is an exception that is sometimes used as an input
  // file when code is testing compiler flags.
  if (!util::is_dev_null_path(args[i])) {
    if (!DirEntry(args[i]).is_regular_file()) {
      LOG("{} is not a regular file, not considering as input file", args[i]);
      state.add_common_arg(args[i]);
      return Statistic::none;
    }
  }

  if (fs::exists(args[i])) {
    LOG("Detected input file: {}", args[i]);
    state.input_files.emplace_back(args[i]);
  } else {
    LOG("Not considering {} an input file since it doesn't exist", args[i]);
    state.add_common_arg(args[i]);
  }
  return Statistic::none;
}

const char*
get_default_object_file_extension(const Config& config)
{
  return config.is_compiler_group_msvc() ? ".obj" : ".o";
}

const char*
get_default_pch_file_extension(const Config& config)
{
  return config.is_compiler_group_msvc() ? ".pch" : ".gch";
}

} // namespace

tl::expected<ProcessArgsResult, core::Statistic>
process_args(Context& ctx)
{
  ASSERT(!ctx.orig_args.empty());

  ArgsInfo& args_info = ctx.args_info;
  Config& config = ctx.config;

  // args is a copy of the original arguments given to the compiler but with
  // arguments from @file and similar constructs expanded. It's only used as a
  // temporary data structure to loop over.
  util::Args args = ctx.orig_args;
  ArgumentProcessingState state;

  state.add_common_arg(args[0]); // Compiler

  std::optional<Statistic> argument_error;
  for (size_t i = 1; i < args.size(); i++) {
    const auto error = process_arg(ctx, args_info, ctx.config, args, i, state);
    if (error != Statistic::none && !argument_error) {
      argument_error = error;
    }
  }

  std::reverse(args_info.debug_prefix_maps.begin(),
               args_info.debug_prefix_maps.end());
  std::reverse(args_info.coverage_prefix_maps.begin(),
               args_info.coverage_prefix_maps.end());

  const bool is_link =
    !(state.found_c_opt || state.found_dc_opt || state.found_S_opt
      || state.found_syntax_only || state.found_analyze_opt);

  if (state.input_files.empty()) {
    LOG_RAW("No input file found");
    return tl::unexpected(Statistic::no_input_file);
  }
  if (state.input_files.size() > 1) {
    if (is_link) {
      LOG_RAW("Called for link");
      return tl::unexpected(
        util::pstr(state.input_files.front()).str().find("conftest.")
            != std::string::npos
          ? Statistic::autoconf_test
          : Statistic::called_for_link);
    } else {
      LOG_RAW("Multiple input files");
      return tl::unexpected(Statistic::multiple_source_files);
    }
  }

  args_info.orig_input_file = state.input_files.front();
  // Rewrite to relative to increase hit rate.
  args_info.input_file =
    core::make_relative_path(ctx, args_info.orig_input_file);

  // Bail out on too hard combinations of options.
  if (state.found_mf_opt && state.found_wp_md_or_mmd_opt) {
    // GCC and Clang behave differently when "-Wp,-M[M]D,wp.d" and "-MF mf.d"
    // are used: GCC writes to wp.d but Clang writes to mf.d. We could
    // potentially support this by behaving differently depending on the
    // compiler type, but let's just bail out for now.
    LOG_RAW("-Wp,-M[M]D in combination with -MF is not supported");
    return tl::unexpected(Statistic::unsupported_compiler_option);
  }

  if (!state.last_seen_msvc_z_debug_option.empty()
      && state.last_seen_msvc_z_debug_option.substr(2) != "7") {
    // /Zi and /ZI are unsupported, but /Z7 is fine.
    LOG("Compiler option {} is unsupported",
        state.last_seen_msvc_z_debug_option);
    return tl::unexpected(Statistic::unsupported_compiler_option);
  }

  // Don't try to second guess the compiler's heuristics for stdout handling.
  if (args_info.output_obj == "-") {
    LOG_RAW("Output file is -");
    return tl::unexpected(Statistic::output_to_stdout);
  }

  // Determine output object file.
  bool output_obj_by_source = args_info.output_obj.empty();
  if (!output_obj_by_source && ctx.config.is_compiler_group_msvc()) {
    if (util::pstr(args_info.output_obj).str().back() == '\\') {
      output_obj_by_source = true;
    } else if (DirEntry(args_info.output_obj).is_directory()) {
      output_obj_by_source = true;
    }
  }

  if (output_obj_by_source && !args_info.input_file.empty()) {
    std::string_view extension;
    if (state.found_analyze_opt) {
      extension = ".plist";
    } else if (state.found_S_opt) {
      extension = ".s";
    } else {
      extension = get_default_object_file_extension(ctx.config);
    }
    args_info.output_obj /= util::with_extension(
      fs::path(args_info.input_file).filename(), extension);
  }

  args_info.orig_output_obj = args_info.output_obj;
  args_info.output_obj = core::make_relative_path(ctx, args_info.output_obj);

  // Determine a filepath for precompiled header.
  if (ctx.config.is_compiler_group_msvc() && args_info.generating_pch) {
    bool included_pch_file_by_source = args_info.included_pch_file.empty();

    if (!included_pch_file_by_source
        && (util::pstr(args_info.orig_included_pch_file).str().back() == '\\'
            || DirEntry(args_info.orig_included_pch_file).is_directory())) {
      LOG("Unsupported folder path value for -Fp: {}",
          args_info.included_pch_file);
      return tl::unexpected(Statistic::could_not_use_precompiled_header);
    }

    if (included_pch_file_by_source && !args_info.input_file.empty()) {
      args_info.included_pch_file =
        util::with_extension(fs::path(args_info.input_file).filename(),
                             get_default_pch_file_extension(ctx.config));
      LOG(
        "Setting PCH filepath from the base source file (during generating): "
        "{}",
        args_info.included_pch_file);
    }
  }

  // Determine output dependency file.

  // On argument processing error, return now since we have determined
  // args_info.output_obj which is needed to determine the log filename in
  // CCACHE_DEBUG mode.
  if (argument_error) {
    return tl::unexpected(*argument_error);
  }

  if (state.found_pch || state.found_fpch_preprocess) {
    args_info.using_precompiled_header = true;
    if (!(config.sloppiness().contains(core::Sloppy::time_macros))) {
      LOG_RAW(
        "You have to specify \"time_macros\" sloppiness when using"
        " precompiled headers to get direct hits");
      LOG_RAW("Disabling direct mode");
      return tl::unexpected(Statistic::could_not_use_precompiled_header);
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
      return tl::unexpected(Statistic::unsupported_source_language);
    }
    args_info.actual_language = state.explicit_language;
  } else if (args_info.actual_language.empty()) {
    args_info.actual_language =
      language_for_file(args_info.input_file, config.compiler_type());
  }

  args_info.output_is_precompiled_header =
    args_info.actual_language.find("-header") != std::string::npos
    || is_precompiled_header(args_info.output_obj);

  if (args_info.output_is_precompiled_header && output_obj_by_source) {
    args_info.orig_output_obj = util::add_extension(
      args_info.orig_input_file, get_default_pch_file_extension(config));
    args_info.output_obj =
      core::make_relative_path(ctx, args_info.orig_output_obj);
  }

  if (args_info.output_is_precompiled_header
      && !(config.sloppiness().contains(core::Sloppy::pch_defines))) {
    LOG_RAW(
      "You have to specify \"pch_defines,time_macros\" sloppiness when"
      " creating precompiled headers");
    return tl::unexpected(Statistic::could_not_use_precompiled_header);
  }

  if (is_link) {
    if (args_info.output_is_precompiled_header) {
      state.add_common_arg("-c");
    } else {
      LOG_RAW("No -c option found");
      // Having a separate statistic for autoconf tests is useful, as they are
      // the dominant form of "called for link" in many cases.
      return tl::unexpected(
        util::pstr(args_info.input_file).str().find("conftest.")
            != std::string::npos
          ? Statistic::autoconf_test
          : Statistic::called_for_link);
    }
  }

  if (args_info.actual_language.empty()) {
    LOG("Unsupported source extension: {}", args_info.input_file);
    return tl::unexpected(Statistic::unsupported_source_language);
  }

  if (args_info.actual_language == "assembler") {
    // -MD/-MMD for assembler file does not produce a dependency file.
    args_info.generating_dependencies = false;
  }

  args_info.direct_i_file = language_is_preprocessed(args_info.actual_language);

  if (config.cpp_extension().empty()) {
    std::string p_language = p_language_for_language(args_info.actual_language);
    config.set_cpp_extension(extension_for_language(p_language).substr(1));
  }

  if (args_info.seen_split_dwarf) {
    if (util::is_dev_null_path(args_info.output_obj)) {
      // Outputting to /dev/null -> compiler won't write a .dwo, so just pretend
      // we haven't seen the -gsplit-dwarf option.
      args_info.seen_split_dwarf = false;
    } else {
      args_info.output_dwo = util::with_extension(args_info.output_obj, ".dwo");
    }
  }

  if (!util::is_dev_null_path(args_info.output_obj)) {
    DirEntry entry(args_info.output_obj);
    if (entry.exists() && !entry.is_regular_file()) {
      LOG("Not a regular file: {}", args_info.output_obj);
      return tl::unexpected(Statistic::bad_output_file);
    }
  }

  if (util::is_dev_null_path(args_info.output_dep)) {
    args_info.generating_dependencies = false;
  }

  fs::path output_dir = args_info.output_obj.parent_path();
  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    LOG("Directory does not exist: {}", output_dir);
    return tl::unexpected(Statistic::bad_output_file);
  }

  // Some options shouldn't be passed to the real compiler when it compiles
  // preprocessed code:
  //
  // -finput-charset=CHARSET (otherwise conversion happens twice)
  // -x CHARSET (otherwise the wrong language is selected)
  if (!state.input_charset_option.empty()) {
    state.add_common_arg(state.input_charset_option);
  }
  if (state.found_pch && !ctx.config.is_compiler_group_msvc()) {
    state.add_common_arg("-fpch-preprocess");
  }
  if (!state.explicit_language.empty()) {
    state.add_common_arg("-x");
    state.add_common_arg(state.explicit_language);
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
      args_info.output_dep = util::with_extension(args_info.output_obj, ".d");
    }

    if (!args_info.dependency_target) {
      fs::path dep_target = args_info.orig_output_obj;

      // GCC and Clang behave differently when "-Wp,-M[M]D,wp.d" is used with
      // "-o" but with neither "-MMD" nor "-MT"/"-MQ": GCC uses a dependency
      // target based on the source filename but Clang bases it on the output
      // filename.
      if (state.found_wp_md_or_mmd_opt && !args_info.output_obj.empty()
          && !state.found_md_or_mmd_opt) {
        if (config.compiler_type() == CompilerType::clang) {
          // Clang does the sane thing: the dependency target is the output file
          // so that the dependency file actually makes sense.
        } else if (config.compiler_type() == CompilerType::gcc) {
          // GCC strangely uses the base name of the source file but with a .o
          // extension.
          dep_target =
            util::with_extension(args_info.orig_input_file.filename(),
                                 get_default_object_file_extension(ctx.config));
        } else {
          // How other compilers behave is currently unknown, so bail out.
          LOG_RAW(
            "-Wp,-M[M]D with -o without -MMD, -MQ or -MT is only supported for"
            " GCC or Clang");
          return tl::unexpected(Statistic::unsupported_compiler_option);
        }
      }

      args_info.dependency_target =
        depfile::escape_filename(util::pstr(dep_target).str());
    }
  }

  if (args_info.generating_stackusage) {
    fs::path default_sufile_name =
      util::with_extension(args_info.output_obj, ".su");
    args_info.output_su = core::make_relative_path(ctx, default_sufile_name);
  }

  if (args_info.generating_callgraphinfo) {
    fs::path default_cifile_name =
      util::with_extension(args_info.output_obj, ".ci");
    args_info.output_ci = core::make_relative_path(ctx, default_cifile_name);
  }

  if (args_info.generating_ipa_clones) {
    args_info.output_ipa = core::make_relative_path(
      ctx, util::add_extension(args_info.orig_input_file, ".000i.ipa-clones"));
  }

  if (state.xarch_args.size() > 1) {
    if (state.xarch_args.find("host") != state.xarch_args.end()) {
      LOG_RAW("-Xarch_host in combination with other -Xarch_* is too hard");
      return tl::unexpected(Statistic::unsupported_compiler_option);
    }
    if (state.xarch_args.find("device") != state.xarch_args.end()) {
      LOG_RAW("-Xarch_device in combination with other -Xarch_* is too hard");
      return tl::unexpected(Statistic::unsupported_compiler_option);
    }
  }

  if (!state.xarch_args.empty()) {
    for (const auto& arch : args_info.arch_args) {
      auto it = state.xarch_args.find(arch);
      if (it != state.xarch_args.end()) {
        args_info.xarch_args.emplace(arch, it->second);
      }
    }
  }

  for (const auto& arch : args_info.arch_args) {
    state.add_compiler_only_arg_no_hash("-arch");
    state.add_compiler_only_arg_no_hash(arch);

    auto it = args_info.xarch_args.find(arch);
    if (it != args_info.xarch_args.end()) {
      args_info.xarch_args.emplace(arch, it->second);

      for (const auto& xarch : it->second) {
        state.add_compiler_only_arg_no_hash("-Xarch_" + arch);
        state.add_compiler_only_arg_no_hash(xarch);
      }
    }
  }

  if (state.hash_full_command_line) {
    state.add_extra_args_to_hash(ctx.orig_args);
  }

  if (diagnostics_color_arg) {
    state.add_compiler_only_arg_no_hash(*diagnostics_color_arg);
  }

  if (ctx.config.depend_mode() && !args_info.generating_includes
      && ctx.config.compiler_type() == CompilerType::msvc) {
    ctx.auto_depend_mode = true;
    args_info.generating_includes = true;
    state.add_compiler_only_arg_no_hash("/showIncludes");
  }

  if (state.found_c_opt) {
    state.add_compiler_only_arg_no_hash(*state.found_c_opt);
  }

  if (state.found_dc_opt) {
    state.add_compiler_only_arg_no_hash(*state.found_dc_opt);
  }

  return state.to_result();
}

bool
is_precompiled_header(const fs::path& path)
{
  fs::path ext = path.extension();
  return ext == ".gch" || ext == ".pch" || ext == ".pth"
         || path.parent_path().extension() == ".gch";
}

bool
option_should_be_ignored(const std::string& arg,
                         const std::vector<std::string>& patterns)
{
  return std::any_of(
    patterns.cbegin(), patterns.cend(), [&arg](const auto& pattern) {
      const auto& prefix =
        std::string_view(pattern).substr(0, pattern.length() - 1);
      return (
        pattern == arg
        || (util::ends_with(pattern, "*") && util::starts_with(arg, prefix)));
    });
}
