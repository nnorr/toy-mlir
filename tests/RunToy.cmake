# Runs toyc over a program and checks its output, for the end-to-end ctest
# entries. Invoked through `cmake -P` from add_test(), because a plain COMMAND
# cannot merge streams or match on output by itself.
#
# Required: -DTOYC=<binary> -DINPUT=<file.toy> -DEXPECT=<regex>
# Optional: -DFLAGS=<semicolon-separated flags>  (default: -emit=jit)
#           -DEXPECT_FAIL=ON  to require a nonzero exit instead of success

if(NOT DEFINED FLAGS)
  set(FLAGS "-emit=jit")
endif()

execute_process(
  COMMAND ${TOYC} ${INPUT} ${FLAGS}
  OUTPUT_VARIABLE Out
  ERROR_VARIABLE Err
  RESULT_VARIABLE Res
)

set(Merged "${Out}${Err}")

if(EXPECT_FAIL)
  if(Res EQUAL 0)
    message(FATAL_ERROR
      "expected ${TOYC} ${FLAGS} to fail on ${INPUT}, but it succeeded\n"
      "--- output ---\n${Merged}")
  endif()
elseif(NOT Res EQUAL 0)
  message(FATAL_ERROR
    "${TOYC} ${FLAGS} ${INPUT} exited with ${Res}\n"
    "--- output ---\n${Merged}")
endif()

if(DEFINED EXPECT AND NOT Merged MATCHES "${EXPECT}")
  message(FATAL_ERROR
    "expected output matching '${EXPECT}'\n"
    "--- output ---\n${Merged}")
endif()

message(STATUS "ok: ${INPUT} ${FLAGS}")
