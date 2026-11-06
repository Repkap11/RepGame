#pragma once

// Lightweight in-game profile timers (REPGAME_PROFILE=1). PROFILE_SCOPE
// wraps a function body; scopes are keyed by a static per-site slot so all
// threads aggregate into the same name (e.g. the light workers). Counts,
// total and max are accumulated atomically and drained into results.jsonl
// metric records as "prof":{"<name>":{"n","us","max_us"}} — the interval is
// metric-record to metric-record.
//
// Cost model: disabled is one check of g_enabled per scope. Enabled costs
// two steady_clock reads per scope; only coarse sites (~10) are
// instrumented, never per-block inner loops.

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>

namespace profile {

struct SlotStats {
    unsigned long long n = 0;
    unsigned long long total_ns = 0;
    unsigned long long max_ns = 0;
};

// Set once at init (results::init) from REPGAME_PROFILE via RunPolicy.
extern bool g_enabled;
void set_enabled( bool enabled );

// Function-local slots; names live as string literals — never freed.
int register_slot( const char *name );

// Drains the accumulators; call once per metric record.
std::unordered_map<std::string, SlotStats> snapshot_and_reset( );

// "prof" JSON object body for results::metric, or "" when disabled.
std::string json_and_reset( );

struct Scope {
    int slot;
    std::chrono::steady_clock::time_point t0;
    explicit Scope( int slot_ );
    ~Scope( );
};

} // namespace profile

#define PROFILE_SCOPE( NAME )                          \
    static const int profile_slot_ = ::profile::register_slot( NAME ); \
    const ::profile::Scope profile_scope_( profile_slot_ );
