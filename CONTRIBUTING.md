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
  (or send a mail to the mailing list) asking for comments on your plans before
  doing the bulk of the work. That way you can avoid potentially wasting time
  on doing something that may need major rework to be accepted, or maybe
  doesn'tend up being accepted at all.
* Is your pull request "work in progress", i.e. you don't think that it's ready
  for merging yet but you want early comments? Then create a draft pull request
  as described in [this Github blog
  post](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
* If you have [Uncrustify](http://uncrustify.sourceforge.net) installed, you
  can run `make uncrustify` to adapt your modifications to ccache's code style.
* Consider [A Note About Git Commit
  Messages](https://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)
  when writing commit messages.

### Code style

#### Formatting

* Use tabs for indenting and spaces for aligning C code.
* Use 4 spaces for indenting other code (and spaces for aligning).
* Put the opening curly brace on a new line when defining a function, otherwise
  at the end of the same line.
* Put no space between function name and the following parenthesis.
* Put one space between if/switch/for/while/do and opening curly brace.
* Always use curly braces around if/for/while/do bodies, even if they only
  contain one statement.
* If possible, keep lines at most 80 character wide for a 2 character tab
  width.
* Use only lowercase names for functions and variables.
* Use only uppercase names for enum items and (with some exceptions) macros.
* Don't use typedefs for structs and enums.
* Use //-style comments.

Tip: Install the tool [Uncrustify(http://uncrustify.sourceforge.net) and then
run `make uncrustify` to fix up source code formatting.

#### Idioms

* Declare variables as late as convenient, not necessarily at the beginning of
  the scope.
* Use NULL to initialize null pointers.
* Don't use NULL when comparing pointers.
* Use format(), x_malloc() and friends instead of checking for memory
  allocation failure explicitly.
* Use str_eq() instead of strcmp() when testing for string (in)equality.
* Consider using str_startswith() instead of strncmp().
* Use bool, true and false for boolean values.
* Use tmp_unlink() or x_unlink() instead of unlink().
* Use x_rename() instead of rename().

#### Other

* Strive to minimize use of global variables.
* Write test cases for new code.
