// WASM multiplayer transport: carries the FramedSocket wire protocol over a
// browser WebSocket. The remote end is a WS->TCP bridge (websockify behind
// nginx) in front of the raw RepGame TCP server, so the on-wire protocol is
// identical to the native clients.
//
// emscripten dispatches WebSocket events on the thread that created the
// socket — here the browser main thread, which is also the game-loop thread —
// so all Multiplayer state stays single-threaded (active is atomic anyway).

#include <emscripten.h>
#include <emscripten/websocket.h>
#include <stdio.h>
#include <string.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/net/packet.hpp"

struct WasmMultiplayerHooks {
    static bool on_open( int eventType, const EmscriptenWebSocketOpenEvent *event, void *userData ) {
        Multiplayer *mp = static_cast<Multiplayer *>( userData );
        pr_debug( "Multiplayer websocket connected" );
        mp->framed_socket.adopt_manual( );
        mp->active = true;
        return true;
    }

    static bool on_message( int eventType, const EmscriptenWebSocketMessageEvent *event, void *userData ) {
        Multiplayer *mp = static_cast<Multiplayer *>( userData );
        // The protocol is binary only; ignore stray text frames.
        if ( !event->isText ) {
            mp->framed_socket.push_recv_bytes( event->data, event->numBytes );
        }
        return true;
    }

    static bool on_error( int eventType, const EmscriptenWebSocketErrorEvent *event, void *userData ) {
        Multiplayer *mp = static_cast<Multiplayer *>( userData );
        pr_debug( "Multiplayer websocket error" );
        mp->active = false;
        mp->framed_socket.flag_error( );
        return true;
    }

    static bool on_close( int eventType, const EmscriptenWebSocketCloseEvent *event, void *userData ) {
        Multiplayer *mp = static_cast<Multiplayer *>( userData );
        pr_debug( "Multiplayer websocket closed: clean=%d code=%u", event->wasClean, event->code );
        mp->active = false;
        mp->framed_socket.mark_closed( );
        // Deleting an already-closed handle is a harmless error return.
        emscripten_websocket_delete( event->socket );
        mp->ws_handle = -1;
        return true;
    }
};

void Multiplayer::init( const char *hostname, const int port ) {
    pr_debug( "using websocket server %s", hostname );

    this->active = false;
    this->portno = port;
    this->prev_player_pos = glm::vec3( 0.0f );
    this->prev_rotation = glm::mat4( 1.0f );

    if ( !emscripten_websocket_is_supported( ) ) {
        pr_debug( "WebSocket not supported in this browser" );
        return;
    }

    // index.js passes a full ws:// or wss:// URL in hostname. A bare host
    // falls back to ws://host:port/.
    char url[ 512 ];
    if ( strncmp( hostname, "ws://", 5 ) == 0 || strncmp( hostname, "wss://", 6 ) == 0 ) {
        snprintf( url, sizeof( url ), "%s", hostname );
    } else {
        snprintf( url, sizeof( url ), "ws://%s:%d/", hostname, port );
    }

    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes( &attrs );
    attrs.url = url;
    attrs.protocols = NULL;
    attrs.createOnMainThread = true;
    this->ws_handle = emscripten_websocket_new( &attrs );
    if ( this->ws_handle <= 0 ) {
        pr_debug( "emscripten_websocket_new failed: %d", this->ws_handle );
        this->ws_handle = -1;
        return;
    }
    emscripten_websocket_set_onopen_callback( this->ws_handle, this, WasmMultiplayerHooks::on_open );
    emscripten_websocket_set_onmessage_callback( this->ws_handle, this, WasmMultiplayerHooks::on_message );
    emscripten_websocket_set_onerror_callback( this->ws_handle, this, WasmMultiplayerHooks::on_error );
    emscripten_websocket_set_onclose_callback( this->ws_handle, this, WasmMultiplayerHooks::on_close );
}

void Multiplayer::flush_outbound( ) {
    while ( this->active ) {
        auto frame = this->framed_socket.pop_send_frame( );
        if ( !frame ) {
            break;
        }
        EMSCRIPTEN_RESULT res = emscripten_websocket_send_binary( this->ws_handle, frame->data( ), static_cast<uint32_t>( frame->size( ) ) );
        if ( res != EMSCRIPTEN_RESULT_SUCCESS ) {
            pr_debug( "websocket send_binary failed: %d", res );
            this->framed_socket.flag_error( );
            break;
        }
    }
}

void Multiplayer::disconnect( ) {
    this->active = false;
    this->framed_socket.mark_closed( );
    if ( this->ws_handle > 0 ) {
        // Forcefully tears down the connection (no close handshake); used
        // when the protocol errored. Graceful shutdown goes through cleanup.
        emscripten_websocket_delete( this->ws_handle );
        this->ws_handle = -1;
    }
}

void Multiplayer::process_events( World &world ) {
    if ( !this->active ) {
        return;
    }
    // Drain any pending sends first so the outbound queue doesn't grow unbounded.
    this->flush_outbound( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error, disconnecting" );
        this->framed_socket.clear_error( );
        this->disconnect( );
        return;
    }

    int event_count = 0;
    while ( event_count < 100 ) {
        event_count++;
        auto payload = this->framed_socket.parse_next_frame( );
        if ( this->framed_socket.had_error( ) ) {
            pr_debug( "Multiplayer recv error, disconnecting" );
            this->framed_socket.clear_error( );
            this->disconnect( );
            return;
        }
        if ( !payload ) {
            // No more complete frames buffered right now.
            return;
        }
        this->handle_frame( world, *payload );
    }
}

void Multiplayer::cleanup( ) {
    pr_debug( "closing multiplayer" );
    if ( this->ws_handle > 0 ) {
        emscripten_websocket_close( this->ws_handle, 1000, "" );
        // The socket may never have connected (no on_close will fire), so
        // delete unconditionally; deleting an already-closed handle is safe.
        emscripten_websocket_delete( this->ws_handle );
        this->ws_handle = -1;
    }
    this->active = false;
    this->pending_diffs.clear( );
}
