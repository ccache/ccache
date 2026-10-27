# Contributing to ccache

Want to contribute to ccache? Awesome!

The preferred way to contribute is to create one or several pull requests (PRs)
on [GitHub](https://github.com/ccache/ccache).

## Contribution guidelines

You should:

- understand the code you submit
- be able to explain and motivate the proposed design and implementation
- take full responsibility for the content of your contribution
- add test cases for your new changes, if applicable
- keep pull requests focused on a single logical change
- write clear [commit messages](#commit-message-conventions) and pull request
  descriptions
- follow the [ccache code style](#code-style)

A PR may be rejected early if:

- the PR description provides no motivation for the changes
- it implements too many changes in one go
- it implements major changes without a preceding design discussion
- it appears to contain substantial amounts of stereotypical AI-generated code
  (such as excessive verbosity, excessive comments or needless abstractions)
  with little or no human contributor oversight and insight

## Use of AI tools

Using AI tools for research, to understand the codebase or to help with
implementation is allowed. However, they should be used to accelerate work, not
replace your own understanding or judgment.

In addition to the contribution guidelines above:

- You should carefully review AI-generated code and trim it to what is actually
  needed. PRs containing large amounts of unreviewed or unnecessary AI-generated
  code may be closed without detailed review.
- You should disclose whether AI has been used for non-trivial code generation
  (see the PR template checklist). Simple code completion is considered trivial.
  AI-assisted proofreading and translation of code comments and documentation do
  not need to be disclosed.
- There is no need to disclose which tool or model was used since that's not
  relevant.
- Contributors should in general handle code review and other discussions
  themselves. Using AI for proofreading or translating your own messages to and
  from English is acceptable, but just blindly relaying messages from an AI
  chatbot during code review or other discussions is not acceptable. Humans will
  take time to review the code, and we expect human contributors to make the
  same commitment in handling submissions themselves.
- AI agents must not submit PRs, issues or comments themselves. Discussions in
  ccache repositories are for humans only.

## Tips

* Have a look in `ARCHITECTURE.md` for an overview of the source code tree.
* If you plan to implement major changes, open a discussion or an issue on
  GitHub asking for comments on your plans before doing the bulk of the work.
  That way you can avoid potentially wasting time on doing something that may
  need major rework to be accepted, or maybe doesn't end up being accepted at
  all.
* Create a draft pull request for "work in progress".

## Commit message conventions

It is preferable, but not mandatory, to format commit messages in the spirit of
[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/). The
commit message subject should look like this:

        <type>: <description>
        <type>(<scope>): <description>

`<description>` is a succinct description of the change:

* Use the imperative, present tense: "Change", not "Changed" nor "Changes".
* Capitalize the first letter.
* No dot (`.`) at the end.

Here is a summary of types used for ccache:

| Type         | Explanation                                                                                              |
|--------------|----------------------------------------------------------------------------------------------------------|
| **build**    | A change of the build system or build configuration.                                                     |
| **bump**     | An increase of the version of an external dependency or an update of a bundled third party package.      |
| **chore**    | A change that doesn't match any other type.                                                              |
| **ci**       | A change of CI scripts or configuration.                                                                 |
| **compat**   | A change to support new or changed behavior in compilers, platforms or other external tools.             |
| **docs**     | A change of documentation only.                                                                          |
| **enhance**  | An enhancement of the code without adding a user-visible feature, e.g. adding still unused utility code. |
| **feat**     | An addition or improvement of a user-visible ccache capability.                                          |
| **fix**      | A correction of behavior that does not match intent or documentation.                                    |
| **perf**     | A performance improvement.                                                                               |
| **refactor** | A restructuring of the existing code without changing its external behavior.                             |
| **style**    | A change of code style.                                                                                  |
| **test**     | An addition or modification of tests or test framework.                                                  |

## Code style

Source code formatting is defined by `.clang-format` in the root directory. The
format is loosely based on [LLVM's code formatting
style](https://llvm.org/docs/CodingStandards.html) with some exceptions. Run
`make format` (or `ninja format` if you use Ninja) to format changes according
to ccache's code style. Or even better: set up your editor to run
`<ccache-top-dir>/misc/clang-format` (or any other Clang-Format version 18
binary) automatically when saving. Newer Clang-Format versions likely also work
fine.

Please follow these conventions:

* Use `UpperCamelCase` for types (e.g. classes and structs).
* Use `UPPER_CASE` names for macros and (non-class) enum values.
* Use `snake_case` for other names (namespaces, functions, variables, enum class
  values, etc.).
* Use an `m_` prefix for non-public member variables.
* Use a `g_` prefix for global mutable variables.
* Use a `k_` prefix for global constants.
* Always use curly braces around if/for/while/do bodies, even if they only
  contain one statement.
