# Contributing to sioxx

Thank you for helping improve sioxx. Contributions of bug fixes, tests,
documentation, and focused features are welcome.

## Before you start

For substantial changes, open an issue first to discuss the problem and the
proposed approach. Small fixes and documentation improvements can go directly
to a pull request.

Keep changes focused. Avoid unrelated refactoring in the same pull request,
and preserve compatibility with C++17 and the supported Linux, macOS, and
Windows builds.

## Set up a development build

Follow the README's [Quick start](README.md#quick-start) instructions to
configure and build the project. Available build settings, including options
for tests, examples, system dependencies, and shared libraries, are listed
under [Installation and configuration](README.md#installation-and-configuration).

## Test your changes

Build and run the complete suite as described in
[Testing](README.md#testing).

Add or update tests for behavior changes and bug fixes. Unit tests belong in
`tests/`; add new test sources to `tests/CMakeLists.txt`.

Changes to transports or end-to-end behavior may also need to be exercised
using the README's [Example test server](README.md#example-test-server).

## Code and API changes

- Follow the style and naming of the surrounding code.
- Keep public headers in `include/sioxx/` and implementation details in
  `src/`.
- Document new or changed public APIs with Doxygen-compatible comments.
- Update examples and user documentation when behavior or configuration
  changes.
- Add user-visible changes to the `[Unreleased]` section of `CHANGELOG.md`.

Do not introduce a newer C++ language requirement without prior discussion.

## Static analysis

Run clang-tidy as described in
[Static analysis](README.md#static-analysis) when changing C++ sources or
headers.

## Documentation

Follow [Building the documentation](README.md#building-the-documentation)
when changing documentation or public APIs. The Sphinx build treats warnings
as errors and should complete without warnings.

## Submit a pull request

Before submitting:

1. Rebase your branch on the current target branch.
2. Build the library and run all relevant tests.
3. Run clang-tidy when the change affects C++ sources or headers.
4. Build the documentation when documentation or public APIs change.
5. Review the diff for unrelated or generated files.

In the pull request, explain the problem, the chosen solution, and how the
change was tested. Link any related issues and call out compatibility,
threading, protocol, or public-API implications.

By contributing, you agree that your contribution is licensed under the
project's MIT License.
