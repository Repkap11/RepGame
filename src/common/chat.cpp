// Shared chat core — see chat.hpp for the model.

#include "common/chat.hpp"

#include "common/Logging.hpp"

static bool chat_name_char( char c ) {
    return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
}

void chat_sanitize_text( std::string &text ) {
    std::string clean;
    clean.reserve( text.size( ) );
    for ( const char c : text ) {
        if ( c >= 32 && c <= 126 ) {
            clean += c;
        }
    }
    const size_t first = clean.find_first_not_of( ' ' );
    if ( first == std::string::npos ) {
        clean.clear( );
    } else {
        clean = clean.substr( first, clean.find_last_not_of( ' ' ) - first + 1 );
    }
    if ( clean.size( ) > CHAT_TEXT_MAX ) {
        clean.resize( CHAT_TEXT_MAX );
    }
    text = std::move( clean );
}

bool chat_valid_name( const std::string &name ) {
    if ( name.empty( ) || name.size( ) > CHAT_NAME_MAX ) {
        return false;
    }
    for ( const char c : name ) {
        if ( !chat_name_char( c ) ) {
            return false;
        }
    }
    return true;
}

void chat_sanitize_name( std::string &name ) {
    std::string clean;
    clean.reserve( name.size( ) );
    for ( const char c : name ) {
        if ( chat_name_char( c ) ) {
            clean += c;
        }
    }
    if ( clean.size( ) > CHAT_NAME_MAX ) {
        clean.resize( CHAT_NAME_MAX );
    }
    name = std::move( clean );
}

bool chat_name_equal( const std::string &a, const std::string &b ) {
    if ( a.size( ) != b.size( ) ) {
        return false;
    }
    for ( size_t i = 0; i < a.size( ); i++ ) {
        const char ca = a[ i ];
        const char cb = b[ i ];
        const char la = ( ca >= 'A' && ca <= 'Z' ) ? static_cast<char>( ca - 'A' + 'a' ) : ca;
        const char lb = ( cb >= 'A' && cb <= 'Z' ) ? static_cast<char>( cb - 'A' + 'a' ) : cb;
        if ( la != lb ) {
            return false;
        }
    }
    return true;
}

static void chat_scrub_printables( std::string &s ) {
    for ( char &c : s ) {
        const unsigned char u = static_cast<unsigned char>( c );
        if ( u < 32 || u > 126 ) {
            c = '?';
        }
    }
}

void ChatLog::push( const ChatMessage &msg ) {
    // The bitmap font only covers ASCII: anything else lands as '?' so a
    // stray byte shows readably instead of as a garbled glyph.
    ChatMessage clean = msg;
    chat_scrub_printables( clean.from );
    chat_scrub_printables( clean.target );
    chat_scrub_printables( clean.text );
    this->messages.push_back( clean );
    while ( this->messages.size( ) > CHAT_LOG_MAX ) {
        this->messages.pop_front( );
    }
    pr_debug( "chat: %s", chat_format( clean ).c_str( ) );
}

void ChatLog::push( ChatKind kind, const std::string &from, const std::string &target, const std::string &text, long tick ) {
    this->push( ChatMessage{ kind, from, target, text, tick } );
}

std::string chat_format( const ChatMessage &msg ) {
    switch ( msg.kind ) {
        case CHAT_KIND_PLAYER:
            return "<" + msg.from + "> " + msg.text;
        case CHAT_KIND_EMOTE:
            return "* " + msg.from + " " + msg.text;
        case CHAT_KIND_WHISPER:
            return msg.from + " -> " + msg.target + ": " + msg.text;
        case CHAT_KIND_SYSTEM:
        case CHAT_KIND_INFO:
        case CHAT_KIND_ERROR:
        default:
            return msg.text;
    }
}

std::vector<std::string> chat_wrap( const std::string &text, float max_w,
                                    const std::function<float( const std::string & )> &measure ) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while ( i < text.size( ) ) {
        // One word: [i, word_end).
        size_t word_end = i;
        while ( word_end < text.size( ) && text[ word_end ] != ' ' ) {
            word_end++;
        }
        const std::string word = text.substr( i, word_end - i );
        // Consume this word plus any trailing spaces with the word.
        i = word_end;
        while ( i < text.size( ) && text[ i ] == ' ' ) {
            i++;
        }

        if ( word.empty( ) ) {
            continue;
        }
        const std::string joined = line.empty( ) ? word : line + " " + word;
        if ( measure( joined ) <= max_w ) {
            line = joined;
            continue;
        }
        if ( !line.empty( ) ) {
            lines.push_back( line );
            line.clear( );
        }
        if ( measure( word ) <= max_w ) {
            line = word;
            continue;
        }
        // Overlong word: hard-break it into max_w slices.
        std::string piece;
        for ( const char c : word ) {
            const std::string next = piece + c;
            if ( !piece.empty( ) && measure( next ) > max_w ) {
                lines.push_back( piece );
                piece.clear( );
            }
            piece += c;
        }
        line = piece;
    }
    lines.push_back( line );
    return lines;
}
