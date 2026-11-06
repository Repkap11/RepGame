#pragma once

// `RepGame inspect <world> <sub> ...` — offline world-file inspector. No GL;
// reads the same files MapStorage writes, read-only. Core functions return
// {ok,json,human} so rep_tests can exercise them without capturing stdout.

#include <string>

namespace inspect {

struct Result {
    bool ok = true;
    std::string json;  // always exactly one JSON object
    std::string human; // text rendering for the default mode
};

// Resolves a world argument to a directory: an arg containing '/' or naming
// an existing directory is used as-is; anything else resolves to
// $HOME/.repgame/<name>. Returns "" when nothing exists.
std::string resolve_world_dir( const char *arg );

Result block( const char *dir, int x, int y, int z );
Result slice( const char *dir, int y, int x1, int z1, int x2, int z2 );
Result column( const char *dir, int x, int z, int y0, int y1 );
Result chunk( const char *dir, int cx, int cy, int cz );
// `near` == nullptr lists everything; otherwise filters to radius around it.
Result entities( const char *dir, const double *near, double radius );
Result player( const char *dir );

// CLI entry: argv is [world, sub, ...]. Returns the process exit code.
int run( int argc, char **argv );

} // namespace inspect
