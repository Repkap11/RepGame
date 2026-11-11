#pragma once

// REPGAME_MPLOAD_TRACE=1: one `MPLOAD t=<ms> <event> ...` line per
// multiplayer chunk-diff load-in event on stdout (client and server).
// Every call site gates on mpload_trace_enabled() — when off the cost is
// one static bool read.
//
// Timestamps are milliseconds since process start from a steady_clock
// pinned at first use, so lines from different code paths share a clock.

#include <chrono>
#include <stdarg.h>
#include <stdio.h>

#include "common/utils/env_config.hpp"

static inline bool mpload_trace_enabled( ) {
    static const bool on = env_enabled( "REPGAME_MPLOAD_TRACE" );
    return on;
}

static inline double mpload_now_ms( ) {
    static const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now( );
    return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now( ) - t0 ).count( ) / 1000.0;
}

static inline long long mpload_now_ms_i( ) {
    return static_cast<long long>( mpload_now_ms( ) );
}

static inline void mpload_tracef( const char *fmt, ... ) {
    printf( "MPLOAD t=%.3f ", mpload_now_ms( ) );
    va_list args;
    va_start( args, fmt );
    vprintf( fmt, args );
    va_end( args );
    printf( "\n" );
    fflush( stdout );
}
