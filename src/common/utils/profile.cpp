#include "common/utils/profile.hpp"

#include <mutex>

#include "common/Logging.hpp"

namespace {

constexpr int SLOT_MAX = 64;

struct Slot {
    const char *name = nullptr;
    std::atomic<unsigned long long> n{ 0 };
    std::atomic<unsigned long long> total_ns{ 0 };
    std::atomic<unsigned long long> max_ns{ 0 };
};

// Slot 0 is the reserved dummy for overflow sites — registration starts
// at index 1 so snapshot_and_reset can skip it.
Slot g_slots[ SLOT_MAX ];
int g_slot_count = 1;
std::mutex g_register_mutex;
bool g_overflow_logged = false;

} // namespace

bool profile::g_enabled = false;

void profile::set_enabled( const bool enabled ) {
    g_enabled = enabled;
}

int profile::register_slot( const char *name ) {
    std::lock_guard<std::mutex> lock( g_register_mutex );
    for ( int i = 0; i < g_slot_count; i++ ) {
        if ( g_slots[ i ].name == name ) {
            return i;
        }
    }
    if ( g_slot_count >= SLOT_MAX ) {
        if ( !g_overflow_logged ) {
            g_overflow_logged = true;
            pr_debug( "REPGAME_PROFILE: slot table full (%d), '%s' unregistered", SLOT_MAX, name );
        }
        // Slot 0 is the reserved dump-all bucket for overflow sites.
        return 0;
    }
    const int slot = g_slot_count++;
    g_slots[ slot ].name = name;
    return slot;
}

std::unordered_map<std::string, profile::SlotStats> profile::snapshot_and_reset( ) {
    std::unordered_map<std::string, SlotStats> out;
    if ( !g_enabled ) {
        return out;
    }
    std::lock_guard<std::mutex> lock( g_register_mutex );
    for ( int i = 1; i < g_slot_count; i++ ) {
        SlotStats st;
        st.n = g_slots[ i ].n.exchange( 0, std::memory_order_relaxed );
        st.total_ns = g_slots[ i ].total_ns.exchange( 0, std::memory_order_relaxed );
        st.max_ns = g_slots[ i ].max_ns.exchange( 0, std::memory_order_relaxed );
        if ( st.n > 0 ) {
            out[ g_slots[ i ].name ] = st;
        }
    }
    return out;
}

std::string profile::json_and_reset( ) {
    const auto snap = snapshot_and_reset( );
    if ( snap.empty( ) ) {
        return "";
    }
    std::string out = "{";
    bool first = true;
    for ( const auto &kv : snap ) {
        out += first ? "\"" : ",\"";
        out += kv.first;
        out += "\":{\"n\":";
        out += std::to_string( kv.second.n );
        out += ",\"us\":";
        out += std::to_string( kv.second.total_ns / 1000 );
        out += ",\"max_us\":";
        out += std::to_string( kv.second.max_ns / 1000 );
        out += "}";
        first = false;
    }
    out += "}";
    return out;
}

profile::Scope::Scope( const int slot_ ) : slot( slot_ ) {
    if ( g_enabled ) {
        t0 = std::chrono::steady_clock::now( );
    }
}

profile::Scope::~Scope( ) {
    if ( !g_enabled ) {
        return;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now( ) - t0 ).count( );
    Slot &s = g_slots[ slot ];
    s.n.fetch_add( 1, std::memory_order_relaxed );
    s.total_ns.fetch_add( ns, std::memory_order_relaxed );
    unsigned long long cur = s.max_ns.load( std::memory_order_relaxed );
    while ( static_cast<unsigned long long>( ns ) > cur &&
            !s.max_ns.compare_exchange_weak( cur, ns, std::memory_order_relaxed ) ) {
    }
}
