# Contributing to ccache

Want to contribute to ccache? Awesome!

## Asking a question?

There are several options:

1. Ask a question in
   [discussions](https://github.com/ccache/ccache/issues/discussions).
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

* Have a look in `ARCHITECTURE.md` for an overview of the source code tree.
* If you plan to implement major changes it is wise to open an issue on GitHub
  (or ask in the Gitter room, or send a mail to the mailing list) asking for
  comments on your plans before doing the bulk of the work. That way you can
  avoid potentially wasting time on doing something that may need major rework
  to be accepted, or maybe doesn't end up being accepted at all.
* Is your pull request "work in progress", i.e. you don't think that it's ready
  for merging yet but you want early comments and CI test results? Then create a
  draft pull request as described in [this Github blog
  post](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
* Please add test cases for your new changes if applicable.
* Please follow the ccache's code style (see the section below).
* Consider [A Note About Git Commit
  Messages](https://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)
  when writing commit messages.

## Code style

Source code formatting is defined by `.clang-format` in the root directory. The
format is loosely based on [LLVM's code formatting
style](https://llvm.org/docs/CodingStandards.html) with some exceptions. Run
`make format` (or `ninja format` if you use Ninja) to format changes according
to ccache's code style. Or even better: set up your editor to run
`<ccache-top-dir>/misc/clang-format` (or any other Clang-Format version 10
binary) automatically when saving. Newer Clang-Format versions likely also work
fine.

Please follow these conventions:

* Use `UpperCamelCase` for types (e.g. classes and structs).
* Use `UPPER_CASE` names for macros and (non-class) enum values.
* Use `snake_case` for other names (namespaces, functions, variables, enum class
  values, etc.). (Namespaces used to be in `UpperCamelCase`; transition is work
  in progress.)
* Use an `m_` prefix for non-public member variables.
* Use a `g_` prefix for global mutable variables.
* Use a `k_` prefix for global constants.
* Always use curly braces around if/for/while/do bodies, even if they only
  contain one statement.
