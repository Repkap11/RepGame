#pragma once

// Server-side chat + player-name arbitration — the fourth feature hub
// next to ServerEntities/ServerContainers/ServerSigns. Stateless about
// history (the server keeps none): it only owns the fd -> effective-name
// table, applies the name rules (sanitize, fallback Player<fd>,
// case-insensitive dedupe with _2/_3 suffixes), stamps every chat line
// with the sender's server-side name (client-sent `from` is ignored so
// it can't be spoofed), and routes whispers target + echo.
//
// Names travel as PLAYER_NAME frames whose frame-header player_id is the
// subject fd; a newly connected client gets one per already-named client
// (the name replay). Join/rename/leave notifications are SYSTEM chat
// lines; every chat line is logged to server stdout.

#include <map>
#include <string>

class Server;
class ServerLogic;
class PacketReader;
enum class NetMsgType : uint8_t;

class ServerChat {
    ServerLogic *logic = nullptr;

    // fd -> effective display name. An fd with no entry reads as
    // "Player<fd>" (chat_valid_name output is non-empty, so an entry
    // always holds a usable name).
    std::map<int, std::string> names;

    // Effective name for a fd: the table entry, else "Player<fd>".
    std::string name_of( int client_fd ) const;
    // Resolve a case-insensitive name to a fd; false when no live name
    // matches (default "Player<fd>" fallbacks don't match).
    bool find_player( const std::string &name_ci, int &fd ) const;
    // Sanitized, deduped candidate: `_2`, `_3`… suffixes appended
    // case-insensitively against the other fds, within CHAT_NAME_MAX.
    std::string dedupe( const std::string &want, int client_fd ) const;
    static void send_name( Server &server, int client_fd, int subject_fd, const std::string &name );
    static void send_chat( Server &server, int client_fd, uint8_t kind, const std::string &from, const std::string &target, const std::string &text, int32_t player_id );
    static void broadcast_chat( Server &server, uint8_t kind, const std::string &from, const std::string &target, const std::string &text, int32_t player_id );
    // SYSTEM line to one client / to everyone.
    static void system_line( Server &server, int client_fd, const std::string &text );
    static void broadcast_system( Server &server, const std::string &text );

  public:
    void init( ServerLogic *logic );

    // Replay the name table to the new client. Called from
    // ServerLogic::on_client_connected.
    void on_client_connected( Server &server, int client_fd );

    // Dispatch from ServerLogic::on_client_message; returns true when the
    // type was consumed (PLAYER_NAME, CHAT — handled or rejected).
    bool on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r );

    // Broadcast the "left the game" line when the fd was named, then drop
    // the entry. Called from ServerLogic::on_client_disconnected.
    void on_client_disconnected( Server &server, int client_fd );

    // Selftest hook: the fd -> name table.
    const std::map<int, std::string> &name_table( ) const { return this->names; }
};
