# `kokusei` developing conventions

## Commenting

- No comments across the code base
- Namespace comments enforced by clang-fotmat is the sole exception

## Formatting the shell

- Command: `clang-format -i --style="{IndentWidth: 4}" <filename>.cpp`

## Module boundary

- A module is not allowed to include files from another module.
- A module shall manage its internal works, without bleeding into `kokusei.cpp`.
- `kokusei.cpp` shall not include specific components belonging to a module.

## Includes

- `meson.build` adds `include_directories('src')` to both the `kokusei` and `kokusei-test` targets.
- Every local `#include` is root-relative from `src/`, e.g. `#include "core/log.h"`, never `../` or a bare filename.
- `test/**` includes `src/` headers the same root-relative way, e.g. `#include "app/config.h"`.
- Generated Wayland protocol headers stay bare filenames since they build outside `src/`.
- Header order:
    - system headers (`<header>`)
    - one blank line
    - local headers:
        - `"dir1/local_header.h"`
        - blank
        - `"dir2/local_header.h`