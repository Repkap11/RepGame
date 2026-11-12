// Server sign-text arbitration — see server_signs.hpp for the model.

#include "server/server_signs.hpp"

#include <stdio.h>

#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/net/packet.hpp"
#include "server/server.hpp"
#include "server/server_logic.hpp"

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerSigns::init( ServerLogic *logic ) {
    this->logic = logic;
}

ServerSigns::ChunkCache &ServerSigns::ensureChunk( const glm::ivec3 &chunk_pos ) {
    ChunkCache &cache = this->chunks[ chunk_pos ];
    if ( !cache.loaded ) {
        cache.loaded = true;
        this->logic->map_storage.load_signs( chunk_pos, cache.texts );
    }
    return cache;
}

void ServerSigns::send_text( Server &server, int client_fd, const glm::ivec3 &pos, const SignText &text ) {
    NetSignTextPayload p;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;
    p.text = text;
    server.send( client_fd, NetMsgType::SIGN_TEXT, p );
}

void ServerSigns::broadcast_text( Server &server, const glm::ivec3 &pos, const SignText &text ) {
    NetSignTextPayload p;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;
    p.text = text;
    PacketWriter w;
    net_serialize_sign_text( w, p );
    // The sender gets the echo too — its optimistic local copy converges
    // to the sanitized/stored text.
    server.broadcast( NetMsgType::SIGN_TEXT, 0, w.buf( ) );
}

bool ServerSigns::on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r ) {
    if ( type != NetMsgType::SIGN_TEXT ) {
        return false;
    }
    NetSignTextPayload p;
    if ( !net_deserialize_sign_text( r, p ) ) {
        pr_debug( "Malformed SIGN_TEXT from:%d", client_fd );
        return true;
    }
    const glm::ivec3 pos( p.x, p.y, p.z );
    // The cell must be a recorded sign: sign blocks only exist through
    // BLOCK_UPDATE edits, so a cell without a recorded sign state (never
    // edited, or edited to something else) can't take text.
    BlockState state;
    if ( !this->logic->recorded_block_at( pos, state ) || !block_is_sign( state.id ) ) {
        pr_debug( "SIGN_TEXT for non-sign at %d,%d,%d from:%d — rejected", pos.x, pos.y, pos.z, client_fd );
        return true;
    }
    SignText clean = p.text;
    sign_sanitize( clean );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ChunkCache &cache = this->ensureChunk( chunk_pos );
    if ( clean.empty( ) ) {
        cache.texts.erase( pos );
    } else {
        cache.texts[ pos ] = clean;
    }
    cache.dirty = true;
    this->logic->scheduleChunkSave( chunk_pos );
    broadcast_text( server, pos, clean );
    return true;
}

void ServerSigns::on_block_update( Server &server, const glm::ivec3 &pos, const BlockState &state ) {
    if ( block_is_sign( state.id ) ) {
        // Sign -> sign (re-place / variant swap): text survives.
        return;
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ChunkCache &cache = this->ensureChunk( chunk_pos );
    const auto it = cache.texts.find( pos );
    if ( it == cache.texts.end( ) ) {
        return;
    }
    cache.texts.erase( it );
    cache.dirty = true;
    this->logic->scheduleChunkSave( chunk_pos );
    // All-empty lines = "cleared" on the wire.
    broadcast_text( server, pos, SignText( ) );
}

void ServerSigns::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_pos ) {
    ChunkCache &cache = this->ensureChunk( chunk_pos );
    for ( const auto &pair : cache.texts ) {
        send_text( server, client_fd, pair.first, pair.second );
    }
}

void ServerSigns::persist_chunk( const glm::ivec3 &chunk_pos ) {
    const auto it = this->chunks.find( chunk_pos );
    if ( it == this->chunks.end( ) || !it->second.dirty ) {
        return;
    }
    this->logic->map_storage.persist_signs( chunk_pos, it->second.texts );
    it->second.dirty = false;
}

void ServerSigns::persist_all( ) {
    for ( auto &[ chunk_pos, cache ] : this->chunks ) {
        (void)chunk_pos;
        if ( cache.dirty ) {
            this->logic->map_storage.persist_signs( chunk_pos, cache.texts );
            cache.dirty = false;
        }
    }
    this->chunks.clear( );
}

void ServerSigns::evict_chunk( const glm::ivec3 &chunk_pos ) {
    this->chunks.erase( chunk_pos );
}

const SignText *ServerSigns::find( const glm::ivec3 &pos ) const {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    const auto cit = this->chunks.find( chunk_pos );
    if ( cit == this->chunks.end( ) ) {
        return nullptr;
    }
    const auto eit = cit->second.texts.find( pos );
    return eit == cit->second.texts.end( ) ? nullptr : &eit->second;
}
