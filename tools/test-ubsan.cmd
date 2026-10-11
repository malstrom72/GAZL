@ECHO OFF
REM Twin of test-ubsan.sh. MSVC has no UndefinedBehaviorSanitizer, so on Windows this only says so; the Linux CI job
REM runs the real check.
ECHO test-ubsan: skipped, MSVC has no UBSan (CI runs tools/test-ubsan.sh on Linux)
EXIT /B 0
