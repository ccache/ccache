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
  for merging yet but you want early comments? Then create a draft pull request
  as described in [this Github blog
  post](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
* If you have [Uncrustify](http://uncrustify.sourceforge.net) installed, you
  can run `make uncrustify` to adapt your modifications to ccache's code style.
* Consider [A Note About Git Commit
  Messages](https://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)
  when writing commit messages.

## Code style

ccache was written in C99 until 2019 when it started being converted to C++11.
The conversion is a slow work in progress, which is why there are lots of
C-style code left. Please refrain from doing large C to C++ conversions at
once; do it little by little.

Tip: Install the tool [Uncrustify(http://uncrustify.sourceforge.net) and then
run `make uncrustify` to fix up source code formatting.

### New code

* Use tabs for indenting and spaces for aligning.
* If possible, keep lines at most 80 character wide for a 2 character tab
  width.
* Put the opening curly brace on a new line when defining a class, struct or
  function, otherwise at the end of the same line.
* Use UpperCamelCase for classes, structs, functions, methods, members and
  global variables.
* Use lowerCamelCase for parameters, arguments and local variables.
* Use UPPER_CASE names for macros.

### Legacy C-style code style

* Put the opening curly brace on a new line when defining a function, otherwise
  at the end of the same line.
* Put no space between function name and the following parenthesis.
* Use UPPER_CASE names for enum values and macros.
* Use snake_case for everything else.

#### Other

* Strive to minimize use of global variables.
* Write test cases for new code.
