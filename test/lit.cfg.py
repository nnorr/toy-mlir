# lit configuration for the toy-mlir end-to-end tests.
#
# Each test is a .toy or .mlir file carrying its own RUN: line and CHECK:
# expectations, the way llvm/test and mlir/test/Examples/Toy work. Run the whole
# suite with `ctest -R lit`, or one file with
# `<llvm-build>/bin/llvm-lit -v build/tests/test/opt/transpose-transpose.toy`.
#
# Every RUN line pipes `2>&1` into FileCheck: MLIR dumps and LLVM IR go to
# stderr in the upstream tutorial, and merging the streams keeps the tests
# indifferent to which stream the driver picked.

import os

import lit.formats

config.name = "toy-mlir"
# lit's internal shell. LLVM 23 deprecated execute_external=True and LLVM 24
# refuses it outright, so RUN lines have to stay within what the internal shell
# understands: pipelines, redirects and && are fine, shell builtins are not.
config.test_format = lit.formats.ShTest()
config.suffixes = [".toy", ".mlir"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.toy_mlir_obj_root, "test")

# The driver under test, plus the tools individual suites need. Anything that
# CMake could not find is left unsubstituted on purpose: a test that needs it
# then fails loudly rather than silently passing.
# Order matters: substitutions are applied in sequence, so %toyc-upstream has to
# be registered before %toyc or the shorter name rewrites its prefix and leaves
# a "-upstream" suffix behind.
config.substitutions.append(("%toyc-upstream", config.toyc_upstream_binary))
config.substitutions.append(("%toyc", config.toyc_binary))
config.substitutions.append(("%filecheck", config.filecheck_binary))
config.substitutions.append(("%clang", config.clang_binary))
config.substitutions.append(("%readelf", config.readelf_binary))
config.substitutions.append(("%dwarfdump", config.dwarfdump_binary))

# FileCheck's directory also holds `not` and `count`, which RUN lines use
# directly the way llvm/test does.
config.environment["PATH"] = os.path.pathsep.join(
    [os.path.dirname(config.filecheck_binary), config.environment.get("PATH", "")]
)

# Tests that need a linker or a DWARF reader are skipped rather than failed when
# the tool is absent, so the suite stays useful on a machine with only MLIR.
if config.clang_binary and os.path.exists(config.clang_binary):
    config.available_features.add("clang")
if config.readelf_binary and os.path.exists(config.readelf_binary):
    config.available_features.add("readelf")
if config.toyc_upstream_binary and os.path.exists(config.toyc_upstream_binary):
    config.available_features.add("upstream-toyc")
