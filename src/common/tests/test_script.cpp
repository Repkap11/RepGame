// rep_tests coverage for the script runner's pure helpers
// (include/common/harness/script.hpp): line tokenizing and assert-op
// evaluation.

#include "test_util.hpp"

#include "common/harness/script.hpp"

static int check_tok( const char *line, const std::vector<std::string> &want ) {
    const std::vector<std::string> got = script::tokenize( line );
    const bool ok = got == want;
    pr_test( "  tokenize %-24s %s", line, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

static int check_cmp( const char *op, const double a, const double b, const bool want ) {
    const bool ok = script::eval_cmp( op, a, b ) == want;
    pr_test( "  cmp %-3s %g %g = %d %s", op, a, b, want, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

static int check_long( const char *tok, const bool want_ok, const long want = 0 ) {
    long v = -12345;
    const bool got_ok = script::parse_long( tok, &v );
    const bool ok = got_ok == want_ok && ( !want_ok || v == want );
    pr_test( "  parse_long %-22s %s", tok, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

static int check_double( const char *tok, const bool want_ok, const double want = 0 ) {
    double v = -12345;
    const bool got_ok = script::parse_double( tok, &v );
    const bool ok = got_ok == want_ok && ( !want_ok || v == want );
    pr_test( "  parse_double %-20s %s", tok, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

int test_script( ) {
    int fails = 0;
    fails += check_tok( "wait 40", { "wait", "40" } );
    fails += check_tok( "  setblock  1 2 3  stone ", { "setblock", "1", "2", "3", "stone" } );
    fails += check_tok( "assert_light 0 1 0 block >= 12", { "assert_light", "0", "1", "0", "block", ">=", "12" } );
    fails += check_tok( "", {} );

    fails += check_cmp( "==", 12, 12, true );
    fails += check_cmp( "!=", 12, 12, false );
    fails += check_cmp( "<", 9, 12, true );
    fails += check_cmp( "<=", 12, 12, true );
    fails += check_cmp( ">", 9, 12, false );
    fails += check_cmp( ">=", 12, 9, true );
    fails += check_cmp( "~=", 1, 1, false ); // bad op is never true

    fails += check_long( "40", true, 40 );
    fails += check_long( "-3", true, -3 );
    fails += check_long( "+7", true, 7 );
    fails += check_long( "1O", false ); // letter O, atoi read this as 1
    fails += check_long( "x", false );
    fails += check_long( "", false );
    fails += check_long( "3.5", false );
    fails += check_long( " 4", false );
    fails += check_long( "99999999999999999999999", false );
    fails += check_double( "1.5", true, 1.5 );
    fails += check_double( "-80", true, -80 );
    fails += check_double( "2e3", true, 2000 );
    fails += check_double( "1.5x", false );
    fails += check_double( "nan", false );
    fails += check_double( "inf", false );
    fails += check_double( "", false );
    return fails;
}
