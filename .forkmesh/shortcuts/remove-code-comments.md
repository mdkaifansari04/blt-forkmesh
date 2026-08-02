# name: Remove comments from code
# description: Strip every comment out of every code file in the repo, leaving the code itself untouched.

Go through all code files in this repository and remove every comment from them.

- Cover every language present in the checkout (C++/headers, Python, JavaScript,
  Dart, shell, CMake, YAML, …) and use each language's own comment syntax:
  line comments, block comments, and doc comments alike.
- Delete the comment text only. Do not change any executable code, string
  literals, or regexes that merely look like comments (`"# not a comment"`,
  `"// not a comment"`, URLs such as `https://…`).
- Keep lines that are load-bearing even though they start with a comment
  marker: shebangs (`#!/usr/bin/env bash`), encoding/pragma lines, licence
  headers required for compliance, linter/compiler directives
  (`# noqa`, `// eslint-disable`, `# type:`, `// clang-format off`, `#pragma`),
  and comment-only markers a build or test asserts on.
- Tidy up what the removal leaves behind: drop the now-empty comment lines and
  any trailing whitespace, but keep the surrounding blank-line structure so the
  diff stays readable.
- Do not touch generated files, vendored third-party trees, or anything ignored
  by git.

When you are done, build the project and run the test suite to prove nothing
broke, then report which files were changed and anything you deliberately left
in place.
