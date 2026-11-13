#pragma once
#include <string>
#include <vector>

struct RepGameState;
class ChunkLoader;

// REPGAME_SCRIPT=<file>: a line-per-command scenario runner driven from
// RepGame::tick. Blocking commands (wait/wait_idle/screenshot) hold the
// program counter until they clear on a later tick; unknown commands and
// failed asserts report through the results stream. Command reference:
// docs/debug-tools.md "Scripted scenarios".
namespace script {
// Pure helpers covered by rep_tests.
std::vector<std::string> tokenize( const std::string &line );
bool eval_cmp( const char *op, double actual, double expected );
// Whole-token numeric args: false on empty, trailing junk, overflow or (for
// doubles) non-finite values — exec_line turns that into a cmd_error.
bool parse_long( const std::string &tok, long *out );
bool parse_double( const std::string &tok, double *out );

// Per-tick driver, called from RepGame::tick (no-op without REPGAME_SCRIPT).
// loader is passed in because World::chunkLoader is RepGame-private.
void tick( RepGameState &state, ChunkLoader &loader );
} // namespace script
