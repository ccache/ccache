# Contributing to ccache

Want to contribute to ccache? Awesome!

## Asking a question?

There are several options:

1. Ask a question in the [issue
   tracker](https://github.com/ccache/ccache/issues/new/choose).
2. Post your question to the [mailing
   list](https://lists.samba.org/mailman/listinfo/ccache/).
3. Chat in the [Gitter room](https://gitter.im/ccache/ccache).

## Reporting an issue?

Please include at least the following information in your bug report:

1. Which version of ccache you use.
2. Which compiler you use, if applicable.
3. Which operating system you use, if applicable.
4. The problematic behavior you experienced (_actual behavior_).
5. How you would like ccache to behave instead (_expected behavior_).
6. Steps to reproduce the problematic behavior.

Also, consider reading [Effective Ways to Get Help from Maintainers](
https://www.snoyman.com/blog/2017/10/effective-ways-help-from-maintainers).

## Contributing code?

The preferred way is to create one or several pull request with your
proposal(s) on [GitHub](https://github.com/ccache/ccache).

Here are some hints to make the process smoother:

* If you plan to implement major changes it is wise to open an issue on GitHub
  (or ask in the Gitter room, or send a mail to the mailing list) asking for
  comments on your plans before doing the bulk of the work. That way you can
  avoid potentially wasting time on doing something that may need major rework
  to be accepted, or maybe doesn't end up being accepted at all.
* Is your pull request "work in progress", i.e. you don't think that it's ready
  for merging yet but you want early comments and CI test results? Then create
  a draft pull request as described in [this Github blog
  post](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
* If you have [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 6.0
  or newer, you can run `make format` to adapt your modifications to ccache's
  code style.
* Consider [A Note About Git Commit
  Messages](https://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)
  when writing commit messages.

## Code style

ccache was written in C99 until 2019 when it started being converted to C++11.
The conversion is a slow work in progress, which is why there is a lot of
C-style code left. Please refrain from doing large C to C++ conversions; do it
little by little.

Source code formatting is defined by `.clang-format` in the root directory.
It's based on [LLVM's code formatting
style](https://llvm.org/docs/CodingStandards.html) with some exceptions. You
can install the [clang-format](https://clang.llvm.org/docs/ClangFormat.html)
6.0 or newer and run `make format` to fix up the source code formatting.

Please follow these conventions:

* Always use curly braces around if/for/while/do bodies, even if they only
  contain one statement.
* Use `UpperCamelCase` for types (e.g. classes and structs) and namespaces.
* Use `UPPER_CASE` names for macros.
* Use `snake_case` for other names (functions, variables, enum values, etc.).
* Use an `m_` prefix for non-public member variables.
* Use a `g_` prefix for global mutable variables.
* Use a `k_` prefix for global constants.
