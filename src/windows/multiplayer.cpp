#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"

// Windows has no multiplayer transport; the shared send-side and dispatch
// code lives in src/common/multiplayer_common.cpp and these are the
// platform hooks it needs. Multiplayer is never initialized, so active is
// always false and nothing here is reachable.

void Multiplayer::init( const char *hostname, int port ) {
}

void Multiplayer::process_events( World &world ) {
}

void Multiplayer::flush_outbound( ) {
}

void Multiplayer::disconnect( ) {
}

void Multiplayer::cleanup( ) {
}
