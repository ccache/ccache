// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include <ccache/hash.hpp>
#include <ccache/hashutil.hpp>

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>

namespace {

std::string
generate_source_code(size_t size)
{
  // Synthetic preprocessed-C++-like code, hopefully reasonably representative
  // of real source code.
  //
  // For our purposes, it's the frequency of _ (for temporal macro matching), .
  // (for incbin matching) and # (for embed matching) that is of most
  // importance.
  static constexpr std::string_view corpus =
    R"(# 42 "/usr/include/foo/bar/banana.h" 3

namespace std _FOO_VISIBILITY(default) {

constexpr double __one = 17.0 / 42.0;
auto __pair = std::make_pair(__one, __one);
__result._some_payload._do_bar(__one);
__state._some_storage._do_foo();

const char* __msg = "hello world";
const char* __file = "/usr/include/hello.h";

template<typename _ForwardIterator, typename _Compare>
inline _ForwardIterator
__some_name(_ForwardIterator __first, _ForwardIterator __last,
            _ForwardIterator __middle, _Compare __comp)
{
  while (true) {
    while (__comp(__first, __middle))
      ++__first;
    --__last;
    while (__comp(__middle, __last))
      --__last;
    if (!(__first < __last))
      return __first;
    std::iter_swap(__first, __last);
    ++__first;
  }
}

// ... cut along line #eh4711 ...

template<typename _MyAwesomeIterator>
inline void
__do_sort(_MyAwesomeIterator __first, _MyAwesomeIterator __last)
{
  for (_MyAwesomeIterator __i = __first + 1; __i != __last; ++__i)
    if (__some::__ops::__do_foo()(__i, __first))
      std::something(__first, __i, __i + 1);
}

template<typename _Tp>
inline _Tp
__what_what(_Tp __one, _Tp __two, _Tp __three)
{
  return foo(bar(__one, __two), __three);
}

}
)";

  std::string source;
  source.reserve(size);

  while (source.size() < size) {
    source += corpus;
  }

  source.resize(size);
  return source;
}

} // namespace

#ifdef HAVE_AVX2
static void
BM_check_for_source_code_patterns_avx2(benchmark::State& state)
{
  const auto source = generate_source_code(state.range(0));
  for (auto _ : state) {
    auto result = check_for_source_code_patterns_avx2(source);
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(state.iterations() * source.size());
}
#endif

static void
BM_check_for_source_code_patterns_scalar(benchmark::State& state)
{
  const auto source = generate_source_code(state.range(0));
  for (auto _ : state) {
    auto result = check_for_source_code_patterns_scalar(source);
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(state.iterations() * source.size());
}

#ifdef HAVE_AVX2
BENCHMARK(BM_check_for_source_code_patterns_avx2)
  ->Arg(1000)
  ->Arg(10000)
  ->Arg(100000)
  ->Arg(1000000);
#endif

BENCHMARK(BM_check_for_source_code_patterns_scalar)
  ->Arg(1000)
  ->Arg(10000)
  ->Arg(100000)
  ->Arg(1000000);
