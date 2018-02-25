# Contributing to ccache

Want to contribute to ccache? Awesome!

## Asking a question?

It's OK to ask questions in the issue tracker for now.

However, please consider posting your question to the
[mailing list](https://lists.samba.org/mailman/listinfo/ccache/) instead, since
you are more likely to get an answer there.

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

Great! You can create a pull request with your proposal on github, or you could
post one or several patches to the
[mailing list](https://lists.samba.org/mailman/listinfo/ccache/).

### How to write commit messages

* Write a summary (short description) on the first line.
* Start the summary with a capital letter. Optional: prefix the short
  description with a context followed by a colon.
* The summary should be in imperative mood (see examples below).
* The summary should not end with a period. It's a title and titles don't end
  with a period.
* If a longer description is wanted, add a second line empty and write the
  longer description on line three and below.
* Keep lines in the message at most 72 characters wide.

Example 1:

    Hash a delimiter string between parts to separate them

    Previously, "gcc -I-O2 -c file.c" and "gcc -I -O2 -c file.c" would hash
    to the same sum.

Example 2:

    win32: Add a space between filename and error string in x_fmmap()

See also:

* http://stopwritingramblingcommitmessages.com
* http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
* https://github.com/erlang/otp/wiki/Writing-good-commit-messages

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

Tip: Install the tool uncrustify <http://uncrustify.sourceforge.net> and then
run "make uncrustify" to fix up source code formatting.

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
