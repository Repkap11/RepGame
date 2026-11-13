// Server chat + name arbitration — see server_chat.hpp for the model.

#include "server/server_chat.hpp"

#include <stdio.h>

#include "common/chat.hpp"
#include "common/net/packet.hpp"
#include "server/server.hpp"
#include "server/server_logic.hpp"

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerChat::init( ServerLogic *logic ) {
    this->logic = logic;
}

std::string ServerChat::name_of( int client_fd ) const {
    const auto it = this->names.find( client_fd );
    if ( it != this->names.end( ) ) {
        return it->second;
    }
    return "Player" + std::to_string( client_fd );
}

bool ServerChat::find_player( const std::string &name_ci, int &fd ) const {
    for ( const auto &pair : this->names ) {
        if ( chat_name_equal( pair.second, name_ci ) ) {
            fd = pair.first;
            return true;
        }
    }
    return false;
}

std::string ServerChat::dedupe( const std::string &want, int client_fd ) const {
    const auto taken = [ this, client_fd ]( const std::string &candidate ) {
        for ( const auto &pair : this->names ) {
            if ( pair.first != client_fd && chat_name_equal( pair.second, candidate ) ) {
                return true;
            }
        }
        return false;
    };
    std::string candidate = want;
    for ( int suffix = 2; taken( candidate ); suffix++ ) {
        const std::string tail = "_" + std::to_string( suffix );
        candidate = want;
        if ( candidate.size( ) + tail.size( ) > CHAT_NAME_MAX ) {
            candidate.resize( CHAT_NAME_MAX - tail.size( ) );
        }
        candidate += tail;
    }
    return candidate;
}

void ServerChat::send_name( Server &server, int client_fd, int subject_fd, const std::string &name ) {
    NetPlayerNamePayload p{ name };
    server.send( client_fd, NetMsgType::PLAYER_NAME, p, subject_fd );
}

void ServerChat::send_chat( Server &server, int client_fd, uint8_t kind, const std::string &from,
                            const std::string &target, const std::string &text, int32_t player_id ) {
    NetChatPayload p{ kind, from, target, text };
    server.send( client_fd, NetMsgType::CHAT, p, player_id );
}

void ServerChat::broadcast_chat( Server &server, uint8_t kind, const std::string &from,
                                 const std::string &target, const std::string &text, int32_t player_id ) {
    NetChatPayload p{ kind, from, target, text };
    PacketWriter w;
    net_serialize_chat( w, p );
    server.broadcast( NetMsgType::CHAT, player_id, w.buf( ) );
}

void ServerChat::system_line( Server &server, int client_fd, const std::string &text ) {
    send_chat( server, client_fd, CHAT_KIND_SYSTEM, "", "", text, 0 );
    pr_debug( "chat->%d %s", client_fd, text.c_str( ) );
}

void ServerChat::broadcast_system( Server &server, const std::string &text ) {
    broadcast_chat( server, CHAT_KIND_SYSTEM, "", "", text, 0 );
    pr_debug( "chat(all) %s", text.c_str( ) );
}

void ServerChat::on_client_connected( Server &server, int client_fd ) {
    // Replay the name table so the new client's player_names map covers
    // everyone already online (the joined line only comes from the
    // newcomer's own PLAYER_NAME broadcast).
    for ( const auto &pair : this->names ) {
        send_name( server, client_fd, pair.first, pair.second );
    }
}

bool ServerChat::on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r ) {
    if ( type == NetMsgType::PLAYER_NAME ) {
        NetPlayerNamePayload p;
        if ( !net_deserialize_player_name( r, p ) ) {
            pr_debug( "Malformed PLAYER_NAME from:%d", client_fd );
            return true;
        }
        chat_sanitize_name( p.name );
        if ( p.name.empty( ) ) {
            p.name = "Player" + std::to_string( client_fd );
        }
        const std::string old_name = this->name_of( client_fd );
        const bool had_name = this->names.count( client_fd ) != 0;
        const std::string effective = this->dedupe( p.name, client_fd );
        if ( had_name && old_name == effective ) {
            return true; // Unchanged: no broadcast, no message.
        }
        this->names[ client_fd ] = effective;
        PacketWriter w;
        NetPlayerNamePayload out{ effective };
        net_serialize_player_name( w, out );
        server.broadcast( NetMsgType::PLAYER_NAME, client_fd, w.buf( ) );
        if ( !had_name ) {
            broadcast_system( server, effective + " joined the game" );
        } else {
            broadcast_system( server, old_name + " is now known as " + effective );
        }
        return true;
    }
    if ( type == NetMsgType::CHAT ) {
        NetChatPayload p;
        if ( !net_deserialize_chat( r, p ) ) {
            pr_debug( "Malformed CHAT from:%d", client_fd );
            return true;
        }
        chat_sanitize_text( p.text );
        if ( p.text.empty( ) ) {
            return true;
        }
        // The client-sent `from` is ignored: the server's name table
        // stamps every line so senders can't spoof.
        const std::string from = this->name_of( client_fd );
        if ( p.kind == CHAT_KIND_WHISPER ) {
            int target_fd = -1;
            if ( !this->find_player( p.target, target_fd ) ) {
                system_line( server, client_fd, "No player named " + p.target );
                return true;
            }
            const std::string target_name = this->names[ target_fd ];
            send_chat( server, target_fd, CHAT_KIND_WHISPER, from, target_name, p.text, client_fd );
            // Echo to the sender so its log shows the whisper it sent —
            // unless the sender whispered to themself and already got it.
            if ( target_fd != client_fd ) {
                send_chat( server, client_fd, CHAT_KIND_WHISPER, from, target_name, p.text, client_fd );
            }
            pr_debug( "chat %s->%s %s", from.c_str( ), target_name.c_str( ), p.text.c_str( ) );
        } else if ( p.kind == CHAT_KIND_PLAYER || p.kind == CHAT_KIND_EMOTE ) {
            broadcast_chat( server, p.kind, from, "", p.text, client_fd );
            pr_debug( "chat <%s> %s", from.c_str( ), p.text.c_str( ) );
        } else {
            // SYSTEM/INFO/ERROR are server->client kinds; a client-sent
            // one is treated as plain player chat.
            broadcast_chat( server, CHAT_KIND_PLAYER, from, "", p.text, client_fd );
            pr_debug( "chat <%s> %s", from.c_str( ), p.text.c_str( ) );
        }
        return true;
    }
    return false;
}

void ServerChat::on_client_disconnected( Server &server, int client_fd ) {
    const auto it = this->names.find( client_fd );
    if ( it == this->names.end( ) ) {
        return;
    }
    broadcast_system( server, it->second + " left the game" );
    this->names.erase( it );
}
