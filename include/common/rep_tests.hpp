#pragma once

// Runs the rep_tests suite (src/common/tests/). argv[0..argc) are optional
// name filters: a test runs when its name contains any filter substring;
// argc == 0 runs every test. Returns nonzero if any selected test fails.
int rep_tests_start( int argc, const char *const *argv );
