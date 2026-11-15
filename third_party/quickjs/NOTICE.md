# Vendored QuickJS-ng

Source: https://github.com/quickjs-ng/quickjs, commit 947ffe4a22426666f209be5835b25d0cb45440fd (version 0.18.0).
Licence: MIT (see `LICENSE` in this folder). Unmodified.

Only the files needed to build the library are included: `dtoa.c`, `libregexp.c`, `libunicode.c`,
`quickjs.c` and the headers they include. The command-line tools, tests and docs are left out.
To update: copy the same files from a newer quickjs-ng release and re-run `ctest`.
