#===----------------------------------------------------------------------===#
# Four kinds of test, in increasing order of how much they cover.
#
#   frontend_unit   FrontendTests.cpp -- the lexer and parser, linked against
#                   ToyFrontend alone. No MLIR is loaded, which is also the
#                   check that AST.h does not depend on the IR.
#   lit             test/**/*.toy|.mlir -- each file carries its own RUN line
#                   and CHECK expectations, the llvm/test idiom. Add a test by
#                   dropping in a file; no CMake change, no rebuild.
#   jit_*           end-to-end: compile and run a program, match its output.
#   compat          the differential sweep against toyc-ch7 over a randomized
#                   corpus. This is the acceptance gate for the repo.
#
# Suites whose tools are missing are skipped with a message rather than failed,
# so the project stays testable on a machine that has only MLIR.
#
# ctest does not build anything: run `cmake --build build` first.
#===----------------------------------------------------------------------===#

#===----------------------------------------------------------------------===#
# Unit tests
#===----------------------------------------------------------------------===#

add_executable(FrontendTests FrontendTests.cpp)
llvm_update_compile_flags(FrontendTests)
target_link_libraries(FrontendTests PRIVATE ToyFrontend)

add_test(NAME frontend_unit COMMAND FrontendTests)

#===----------------------------------------------------------------------===#
# Tools the other suites need
#===----------------------------------------------------------------------===#

# LLVM_TOOLS_BINARY_DIR comes from the LLVM we were configured against, so these
# resolve to the same build that provides MLIR rather than to whatever is on
# PATH.
find_program(LIT_BINARY NAMES llvm-lit lit HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(FILECHECK_BINARY NAMES FileCheck
  HINTS ${LLVM_TOOLS_BINARY_DIR} ${LLVM_TOOLS_BINARY_DIR}/../libexec/llvm)
find_program(TOYC_UPSTREAM_BINARY NAMES toyc-ch7 HINTS ${LLVM_TOOLS_BINARY_DIR})
find_program(READELF_BINARY NAMES readelf llvm-readelf)
find_program(DWARFDUMP_BINARY NAMES llvm-dwarfdump)
find_package(Python3 COMPONENTS Interpreter QUIET)

# The C compiler this project was configured with is also the linker driver for
# the object-file test: it is known to exist and known to match the target,
# which searching PATH for "clang" is not.
set(CLANG_BINARY "${CMAKE_C_COMPILER}")

#===----------------------------------------------------------------------===#
# lit suite
#===----------------------------------------------------------------------===#

if(LIT_BINARY AND FILECHECK_BINARY)
  set(TOYC_BINARY $<TARGET_FILE:toyc>)
  set(TOY_MLIR_TEST_SOURCE_DIR ${PROJECT_SOURCE_DIR}/test)

  # Two steps: configure_file expands @VARS@ but not generator expressions,
  # file(GENERATE) does the reverse, and $<TARGET_FILE:toyc> needs the second.
  configure_file(
    ${PROJECT_SOURCE_DIR}/test/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/test/lit.site.cfg.py.tmp
    @ONLY
  )
  file(GENERATE
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test/lit.site.cfg.py
    INPUT  ${CMAKE_CURRENT_BINARY_DIR}/test/lit.site.cfg.py.tmp
  )

  # llvm-lit finds its own lit module through a path relative to itself, which
  # breaks when the LLVM build tree is reached through a symlink. Pointing
  # PYTHONPATH at lit's source is the fix that does not depend on how the build
  # tree is spelled.
  add_test(NAME lit
    COMMAND ${CMAKE_COMMAND} -E env
      "PYTHONPATH=${LLVM_BUILD_MAIN_SRC_DIR}/utils/lit"
      ${LIT_BINARY} -sv ${CMAKE_CURRENT_BINARY_DIR}/test)
  message(STATUS "IR tests enabled: ${LIT_BINARY} + ${FILECHECK_BINARY}")
else()
  message(STATUS "IR tests disabled (need llvm-lit and FileCheck)")
endif()

#===----------------------------------------------------------------------===#
# End-to-end
#===----------------------------------------------------------------------===#

# transpose([[1,2],[3,4]]) squared element-wise, run through the JIT and then
# through the object-file path, which must agree.
add_test(NAME jit_transpose
  COMMAND ${CMAKE_COMMAND}
    -DTOYC=$<TARGET_FILE:toyc>
    -DINPUT=${PROJECT_SOURCE_DIR}/test/lowering/jit.toy
    -DFLAGS=-emit=jit
    "-DEXPECT=1\\.000000 9\\.000000"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunToy.cmake
)

add_test(NAME jit_transpose_opt
  COMMAND ${CMAKE_COMMAND}
    -DTOYC=$<TARGET_FILE:toyc>
    -DINPUT=${PROJECT_SOURCE_DIR}/test/lowering/jit.toy
    "-DFLAGS=-emit=jit;-opt"
    "-DEXPECT=4\\.000000 16\\.000000"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunToy.cmake
)

# An error path, so a driver that stops reporting failures cannot pass.
add_test(NAME error_redeclaration
  COMMAND ${CMAKE_COMMAND}
    -DTOYC=$<TARGET_FILE:toyc>
    -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/compat/fixtures/redeclaration.toy
    -DFLAGS=-emit=mlir
    -DEXPECT_FAIL=ON
    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunToy.cmake
)

#===----------------------------------------------------------------------===#
# Documentation
#===----------------------------------------------------------------------===#

# check-docs.py makes two passes over the markdown. It runs every command in a
# console block and compares the output, which catches a source location or a
# column that moved. It also checks that code blocks quoting this project's C++
# and TableGen still match the source, which catches a comment edit leaving a
# document quoting text that no longer exists. Between them they keep an edit to
# the prose, or to the compiler, from silently invalidating a document.
#
# It needs toyc, and skips with a message when toyc has not been built yet, so a
# fresh configure plus `ctest` reports a pass rather than a spurious failure.
if(Python3_Interpreter_FOUND)
  add_test(NAME docs
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check-docs.py
      --root ${PROJECT_SOURCE_DIR}
      --toyc $<TARGET_FILE:toyc>
      --frontend-tests $<TARGET_FILE:FrontendTests>
  )
  if(TOYC_UPSTREAM_BINARY)
    set_tests_properties(docs PROPERTIES
      ENVIRONMENT "TOYC_UPSTREAM=${TOYC_UPSTREAM_BINARY}")
  endif()
  message(STATUS "Documentation check enabled: ${Python3_EXECUTABLE}")
else()
  message(STATUS "Documentation check disabled (need python3)")
endif()

#===----------------------------------------------------------------------===#
# Differential sweep against upstream
#===----------------------------------------------------------------------===#

if(TOYC_UPSTREAM_BINARY AND Python3_Interpreter_FOUND)
  # 200 generated programs on a fixed seed keeps this around a minute on a many
  # core machine; COMPAT_COUNT raises it for a deeper run by hand:
  #   COMPAT_COUNT=5000 tests/compat/compare-upstream.sh
  add_test(NAME compat
    COMMAND ${CMAKE_COMMAND} -E env
      TOYC=$<TARGET_FILE:toyc>
      TOYC_UPSTREAM=${TOYC_UPSTREAM_BINARY}
      COMPAT_COUNT=200
      COMPAT_SEED=20260921
      ${CMAKE_CURRENT_SOURCE_DIR}/compat/compare-upstream.sh
  )
  set_tests_properties(compat PROPERTIES TIMEOUT 1800)
  message(STATUS "Differential sweep enabled against ${TOYC_UPSTREAM_BINARY}")

  # The error-path fixtures are only checkable once their diffs have been
  # recorded, which needs a working toyc:
  #   tests/compat/compare-upstream.sh --fixtures-only --update
  file(GLOB COMPAT_EXPECTED "${CMAKE_CURRENT_SOURCE_DIR}/compat/expected/*.diff")
  if(COMPAT_EXPECTED)
    add_test(NAME compat_fixtures
      COMMAND ${CMAKE_COMMAND} -E env
        TOYC=$<TARGET_FILE:toyc>
        TOYC_UPSTREAM=${TOYC_UPSTREAM_BINARY}
        ${CMAKE_CURRENT_SOURCE_DIR}/compat/compare-upstream.sh --fixtures-only
    )
  else()
    message(STATUS
      "compat fixtures: no recorded diffs yet -- run "
      "tests/compat/compare-upstream.sh --fixtures-only --update")
  endif()
else()
  message(STATUS
    "Differential sweep disabled (need toyc-ch7 and python3)")
endif()
