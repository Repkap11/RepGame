#pragma once
#include <string>
#include <glm/glm.hpp>

#include "common/chunk_loader.hpp" // LoaderDebugStats

struct RepGameState;

// Structured run-results writer (REPGAME_RESULTS=<path>): one JSONL event
// stream per run — a "header" line, periodic "metric" records, "event"
// records, and exactly one "summary" line at clean exit (a missing summary
// means the process died abnormally). Record schema lives in
// docs/debug-tools.md. Completely no-op when the env var is unset; lines are
// flushed per record and writes are mutex-protected.
namespace results {
// Pure record formatters (no trailing newline) — rep_tests cover these.
std::string format_metric( long tick, int fly_phase, int rd, const glm::dvec3 &pos, const LoaderDebugStats &s, const std::string &light_json,
                           const std::string &prof_json = "" );
std::string format_event( long tick, const char *name, const std::string &detail_json );
std::string format_probe( long tick, const glm::ivec3 &pos, int block, const char *name, int rotation, int sky, int blocklight );
std::string format_assert( long tick, int line, const char *cmd, bool ok, const char *expected, const char *actual );
std::string format_summary( long ticks, const char *exit_reason, int asserts_total, int asserts_failed, long settle_tick, const std::string &shots_json );
// light_dbg_stats' "k=v k=v | us: fin=u(lfin=v[...]) ..." text → the JSON
// object for a metric's "light" member. Keys after "| us:" are prefixed
// "us_" (they collide with the counter names); numeric values stay numbers.
std::string light_json_from_dbg( const char *dbg );

bool active( );
// Whether a metric record is due this tick (REPGAME_RESULTS_EVERY cadence).
bool metric_due( long tick );
// Once at game init: opens REPGAME_RESULTS and writes the header line.
void init( const char *world_name );
// One metric record; s/light_dbg come from the same probe the FLYMET line
// uses so the results stream never steals a light-dbg interval sample.
void metric( const RepGameState &state, const LoaderDebugStats &s, const char *light_dbg );
// Immediate {"type":"event","name":...} line; fmt is a printf fragment for
// the "detail" object's contents (may be null).
void event( long tick, const char *name, const char *fmt = nullptr, ... );
// loader is passed in because World::chunkLoader is RepGame-private.
void probe( const RepGameState &state, ChunkLoader &loader, const glm::ivec3 &pos );
void assert_( const RepGameState &state, int line, const char *cmd, bool ok, const char *expected, const char *actual );
// Called at each exit path (fly_ticks, capture_done, walktest_ticks,
// signal); unset falls back to "quit" in summary().
void note_exit_reason( const char *reason );
// Emits the quit event + the summary line; called from RepGame::cleanup.
void summary( const RepGameState &state );
} // namespace results
