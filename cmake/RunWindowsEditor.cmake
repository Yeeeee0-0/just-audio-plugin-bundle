if(NOT DEFINED EXECUTABLE OR NOT DEFINED BUNDLE OR NOT DEFINED SLUG OR NOT DEFINED EVIDENCE_ROOT)
    message(FATAL_ERROR "Windows editor test runner requires executable, bundle, slug and evidence root")
endif()
string(TIMESTAMP stamp "%Y%m%dT%H%M%S" UTC)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(evidence "${EVIDENCE_ROOT}/${SLUG}-${stamp}-${nonce}")
# The executable creates this unique directory; do not pre-create it.
execute_process(COMMAND "${EXECUTABLE}" "${BUNDLE}" "${SLUG}" "${evidence}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 120)
message(STATUS "${output}")
message(STATUS "Windows native editor evidence: ${evidence}")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Windows native editor failed (${result}): ${error}")
endif()
