#pragma once

// Multiplayer chat core shared by the client, the server, and the wire
// protocol — GL-free, like sign.hpp. Owns the ChatKind vocabulary, the
// name/text bounds, the sanitizers, one ChatMessage, the client-side
// ChatLog ring buffer, and the pure formatting/wrapping helpers.

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

// Display-name bound (wire name fields + the server name table).
#define CHAT_NAME_MAX 16
// Text bound for one chat line (wire + the input box).
#define CHAT_TEXT_MAX 256
// Session-only client log ring (no server replay, no log file).
#define CHAT_LOG_MAX 100
// Input-box history depth (the client's own sent lines).
#define CHAT_HISTORY_MAX 32

// Wire kinds are 0..3 and shared with NetChatPayload; INFO/ERROR are
// client-only (command output) and never go on the wire.
enum ChatKind : uint8_t {
    CHAT_KIND_PLAYER = 0,
    CHAT_KIND_SYSTEM = 1,
    CHAT_KIND_WHISPER = 2,
    CHAT_KIND_EMOTE = 3,
    CHAT_KIND_INFO = 4,
    CHAT_KIND_ERROR = 5,
};

// Keep printable ASCII only, trim, truncate to CHAT_TEXT_MAX. Idempotent.
void chat_sanitize_text( std::string &text );

// Valid names are 1..CHAT_NAME_MAX chars from [A-Za-z0-9_].
bool chat_valid_name( const std::string &name );
// Drop invalid chars and truncate to CHAT_NAME_MAX; the result may be
// empty (callers pick a fallback).
void chat_sanitize_name( std::string &name );

// Case-insensitive name compare — the one dedupe/lookup predicate shared
// by the client (find_player) and the server (name table).
bool chat_name_equal( const std::string &a, const std::string &b );

// One log line. `tick` is the receiving client's tick_number (0 ok) —
// the closed-chat overlay fades messages older than a few seconds.
struct ChatMessage {
    ChatKind kind = CHAT_KIND_PLAYER;
    std::string from;
    std::string target;
    std::string text;
    long tick = 0;
};

// Client-side session log: a deque capped at CHAT_LOG_MAX. push() also
// pr_debugs the formatted line so headless runs (mp_test, rep-dbg) see
// the conversation on stdout.
class ChatLog {
    std::deque<ChatMessage> messages;

  public:
    void push( const ChatMessage &msg );
    void push( ChatKind kind, const std::string &from, const std::string &target, const std::string &text, long tick = 0 );
    void clear( ) { this->messages.clear( ); }
    const std::deque<ChatMessage> &entries( ) const { return this->messages; }
};

// One display line: "<from> text", "* from text" (emote),
// "from -> target: text" (whisper), plain text for system/info/error.
std::string chat_format( const ChatMessage &msg );

// Pure word wrap against a caller-supplied text-width measure (client
// passes font.getTextWidth; tests pass a fixed-width one). Words wider
// than a line hard-break mid-word. Empty input wraps to one empty line.
std::vector<std::string> chat_wrap( const std::string &text, float max_w,
                                    const std::function<float( const std::string & )> &measure );
