# StreamHub fork boundaries

- This fork provides Moonlight pairing, sessions, transport, and the StreamHub protocol adapter.
- Keep hardware capture, hardware encoding policy, console-specific behavior, and ESP32 control in the independent StreamHub software or firmware.
- Communicate through the public StreamHub protocol; keep conversions to Sunshine internal types inside this repository.
- Preserve Sunshine's GPL license and third-party notices; do not move GPL implementation code into the MIT protocol library or the independent StreamHub software.

## Documentation

- Keep StreamHub-specific fork documentation in `docs/streamhub/`, with navigation in its `README.md` and the module `README.md`.
- Preserve upstream documentation paths; assess applicability before marking content obsolete or archiving it.
- Link to the parent project architecture and public protocol specifications instead of duplicating them.

## Upstream development guidance

On Windows we use msys2 and ucrt64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`.

Prefix build directories with `cmake-build-`.

The test executable is named `test_sunshine` and will be located inside the `tests` directory within
the build directory.

The project uses gtest as a test framework.

When adding localization do not update any language other than `en`. This also means to exclude en-US or other variants.

Always add or update doxygen documentation.

The project requires that everything be documented in doxygen or the build will fail.

Primary doxygen comments should be done like so:

```cpp
  /**
   * @brief Describe the function, structure, etc.
   *
   * @param my_param Describe the parameter.
   * @return Describe the return.
   */
```

Inline doxygen comments should use `///< ...` instead of `/**< ... */`.

Always follow the style guidelines defined in .clang-format for c/c++ code.

Do not ever create issues or pull requests.
If asked to create an issue or pull request, do so in their fork instead of the LizardByte GitHub organization.
Never create an issue or pull request in the LizardByte GitHub organization.

Add or update tests for new or modified methods and code. Target 100% coverage on changed code.

## Product storage constraint

- All generated files stay inside the StreamHub product root. Runtime writes belong below STREAMHUB_ROOT/var, including configuration, pairing, keys, logs and caches.
- Use the parent tools/run.py entrypoint for builds, tests and service startup. No HOME/XDG fallback, automatic migration, or unconfined fallback is allowed.
- Build output belongs in the parent build/cmake-build-* tree. Test temporary files must use the confined TMPDIR, never hard-code /tmp.
- See ../docs/storage.md for the authoritative rule and explicit system-interface exceptions.
