# toy-mlir

A rebuild of the [MLIR Toy tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/) as
one `toyc` binary built out of tree, with the code split into modules instead of
copied per chapter.

The language is the tutorial's, unchanged, and the output is checked against
`toyc-ch7` at every stage. Design notes are in
[ARCHITECTURE.md](ARCHITECTURE.md), per-component walkthroughs in
[`docs/`](docs/README.md), and the upstream tutorial is vendored under
`reference/` so the two can be diffed.

## Requirements

* LLVM and MLIR 24.0.0, built or installed with the ExecutionEngine enabled
  (`-DMLIR_ENABLE_EXECUTION_ENGINE=ON`). CMake fails early if it is missing.
* CMake 3.20 or newer, Ninja, a C++17 compiler.
* Optional, for the test suites: `llvm-lit` and `FileCheck` for the IR tests,
  `python3` and `toyc-ch7` for the differential sweep. Missing tools skip a
  suite with a message instead of failing the configure.

On this machine MLIR lives in `~/dev/08_mlir_toy/build`, which is a symlink: the
tree was configured under that path and moved afterwards, and its exported CMake
files hold the original absolute path.

## Build

```bash
env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS \
  cmake -S . -B build -G Ninja \
    -DCMAKE_C_COMPILER=$HOME/miniconda3/envs/iree-clang/bin/clang \
    -DCMAKE_CXX_COMPILER=$HOME/miniconda3/envs/iree-clang/bin/clang++ \
    -DMLIR_DIR=$HOME/dev/08_mlir_toy/build/lib/cmake/mlir \
    -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

The `env -u` clears the conda compiler variables. Conda exports `CXXFLAGS` and
`LDFLAGS` that CMake picks up as the initial flags, which overrides the build
type and can drop libraries at link time.

`MLIR_DIR` is the only path you need; `MLIRConfig.cmake` brings LLVM with it.
Configure prints which optional suites it found:

```
-- IR tests enabled: .../bin/llvm-lit + .../bin/FileCheck
-- Differential sweep enabled against .../bin/toyc-ch7
```

## Run

`toyc` writes textual output to stderr, as the tutorial's driver does. Use `-o`
to send it to a file.

```bash
# The AST, in the tutorial's format or the one used by 06_llvm_tutorial
build/bin/toyc reference/tests/Ch1/ast.toy -emit=ast
build/bin/toyc reference/tests/Ch1/ast.toy -emit=ast --dump-ast-style=kaleidoscope

# IR at each level
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir -opt
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir-affine -opt
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir-llvm
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=llvm -opt

# Compile and run in this process
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=jit
# 1.000000 16.000000
# 4.000000 25.000000
# 9.000000 36.000000

# Read MLIR back in instead of Toy source
build/bin/toyc prog.mlir -x mlir -emit=mlir-affine

# Which passes would run
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir-affine -opt --print-pipeline
```

Beyond the tutorial, `toyc` also emits object files and debug info:

```bash
# Native object, then link and run it
build/bin/toyc prog.toy -c -o prog.o
nm -g prog.o                                  # T main, U printf, U malloc, U free
clang prog.o -o prog && ./prog                # same output as -emit=jit

# ...with a DWARF line table pointing back at the Toy source
build/bin/toyc prog.toy -c -g -o prog.o
readelf --debug-dump=decodedline prog.o       # CU: prog.toy
readelf --debug-dump=info prog.o | grep producer   # toyc (toy-mlir)

# LLVM IR beside the object file
build/bin/toyc prog.toy -c --emit-llvm -o prog.o    # also writes prog.ll
```

`--target=`, `-mattr=` and `-mabi=` pass a triple, a feature string and an ABI
name to the code generator, which accepts any target the LLVM it links was built
with. The MLIR build here was configured with `LLVM_TARGETS_TO_BUILD=host`, so
only the host backend exists and another triple is rejected:

```
$ build/bin/toyc prog.toy -c --target=riscv64-unknown-elf -o prog.o
Could not create TargetMachine: No available targets are compatible with triple
"riscv64-unknown-elf"                                             # exit 9
```

## Options

```
toyc <input.toy|input.mlir|-> [options]

  -x toy|mlir            input kind (default: toy, or mlir for a .mlir file)
  -emit=<stage>          ast, mlir, mlir-affine, mlir-llvm, llvm, jit
  -opt                   enable the optimization passes
  -o <file>              write output to <file> instead of stderr
  -c                     emit a native object file
  --emit-llvm            with -c, also write the LLVM IR beside the object
  -g                     attach debug info, giving the object a DWARF line table
  --dump-ast-style=<s>   toy (default) or kaleidoscope, for -emit=ast
  --target=<triple>      target for -c (default: host)
  -mattr=<features>      target feature string for -c, e.g. +m,+f,+d
  -mabi=<abi>            target ABI name for -c
  --print-pipeline       print the pass pipeline that would run, then exit
```

MLIR's own options are registered too, so `--mlir-print-ir-after-all`,
`--mlir-print-debuginfo` and `--mlir-pass-statistics` work.

## Test

```bash
ninja -C build && ctest --test-dir build        # 9/9, about 17 s
```

| Suite | What it covers |
| --- | --- |
| `frontend_unit` | 63 checks over the lexer and parser, linked against `ToyFrontend` alone |
| `lit` | 24 IR tests, each a `.toy` or `.mlir` file carrying its own `RUN` and `CHECK` lines |
| `jit_transpose`, `jit_transpose_opt` | compile and run a program, match its printed values |
| `error_redeclaration` | an error path, so a driver that stops reporting failures cannot pass |
| `docs` | runs every command quoted in the markdown and diffs its real output |
| `dumps` | regenerates the IR dumps in `docs/examples/dumps/` and compares them |
| `compat` | the differential sweep against `toyc-ch7`; this is the acceptance gate |
| `compat_fixtures` | 10 fixtures, 20 recorded diffs, for the error paths where this repo deliberately differs |

Individual suites:

```bash
build/tests/FrontendTests                       # unit tests

PYTHONPATH=$HOME/dev/08_mlir_toy/llvm-project/llvm/utils/lit \
  $HOME/dev/08_mlir_toy/build/bin/llvm-lit -sv build/tests/test

tests/compat/compare-upstream.sh                # 240 programs, 2880 comparisons
COMPAT_COUNT=1500 tests/compat/compare-upstream.sh   # a deeper run
```

The sweep generates random valid Toy programs, runs both compilers over them and
over `reference/tests/**` at every stage with and without `-opt`, and fails on
any difference that is not a documented deviation. The default run compares 240
programs in 2880 comparisons; 1500 generated programs give 18480 comparisons in
about 80 seconds. Both report 0 unexpected differences and 0 skipped.
`tests/compat/EXPECTED-DIFFS.md` lists what may differ and shows the measured
evidence for each.

Adding a lit test needs no CMake change: drop a `.toy` file with a `RUN` line
into `test/`.

## Layout

```
include/toy/        14 headers, plus Ops.td and ShapeInferenceInterface.td
src/                Lexer, Parser, ASTDumper        -> ToyFrontend
src/dialect/        ops, types, folders, interfaces -> ToyDialect
src/passes/         shape inference, both lowerings -> ToyPasses
src/                MLIRGen, Pipeline, Translate,
                    DebugInfo, ObjectEmitter, Jit   -> ToyCodegen
src/main.cpp        the driver                       -> toyc
test/               lit suite
tests/              unit tests, and tests/compat for the sweep
docs/               per-component walkthroughs
reference/          the upstream tutorial, verbatim, for diffing
```

6765 lines of code and ODS, 2528 lines of tests. TableGen generates another 6704
lines of C++ from the 595 lines of `.td`.

## License

Apache 2.0 with LLVM exceptions, the same as the tutorial this derives from. See
`LICENSE.TXT`. Files adapted from upstream keep their original header.
