#pragma once
#include <stdlib.h>

// Centralized REPGAME_* environment access. Helpers read the process
// environment at call time; sites that need a value once cache it in a
// static at the use site, as before — these only remove the repeated
// getenv/atoi/atof boilerplate and its null handling.
//
// Two polarity conventions exist in the tree and both collapse to
// env_bool(name, fallback): "REPGAME_X=1 enables" is env_bool(name, false),
// "REPGAME_X=0 disables" is env_bool(name, true).

static inline const char *env_get( const char *name ) {
    return getenv( name );
}

// Set at all — for pure presence switches (REPGAME_MOB_TEST, ...).
static inline bool env_is_set( const char *name ) {
    return getenv( name ) != nullptr;
}

// Set and atoi(value) != 0 — for explicit opt-ins like REPGAME_WEBGPU=1.
static inline bool env_enabled( const char *name ) {
    const char *v = getenv( name );
    return v != nullptr && atoi( v ) != 0;
}

static inline bool env_bool( const char *name, bool fallback ) {
    const char *v = getenv( name );
    return v != nullptr ? atoi( v ) != 0 : fallback;
}

static inline int env_int( const char *name, int fallback ) {
    const char *v = getenv( name );
    return v != nullptr ? atoi( v ) : fallback;
}

static inline long env_long( const char *name, long fallback ) {
    const char *v = getenv( name );
    return v != nullptr ? atol( v ) : fallback;
}

static inline float env_float( const char *name, float fallback ) {
    const char *v = getenv( name );
    return v != nullptr ? static_cast<float>( atof( v ) ) : fallback;
}

static inline double env_double( const char *name, double fallback ) {
    const char *v = getenv( name );
    return v != nullptr ? atof( v ) : fallback;
}
