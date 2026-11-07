// player.dat text format (version 1):
//
//   # RepGame player data
//   format 1
//   pos <x> <y> <z>                   (%.17g doubles)
//   angles <angle_H> <angle_V>        (%.9g floats)
//   flying <0|1>
//   no_clip <0|1>
//   draw_quality <int>
//   game_mode <int>
//   peaceful <0|1>
//   world_time <int64>
//   render_distance <int>
//   min_ambient <float>
//   hotbar_selected <int>
//   hotbar <slot> <item_id> <quantity>     (one line per non-empty slot)
//   survival <slot> <item_id> <quantity>
//
// One `key value` line per field; '#' lines are comments, blank lines are
// skipped and '\r' is tolerated. Unknown keys are ignored (forward
// compatible); missing keys keep PlayerData's defaults; malformed lines are
// ignored. A file is treated as this format iff its first non-comment line
// is `format <n>` — it must then also contain `pos` to load. Older binary
// files keep loading through the frozen layouts below; a downgraded binary
// sees a wrong-size file and falls back to a fresh spawn.

#include "common/player_data.hpp"
#include "common/Logging.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Frozen legacy binary layouts. Never edit these — they exist only to read
// saves written by old binaries. `long` stays `long` on purpose: it was 4
// bytes in WASM/Windows saves and 8 in Linux saves, so each layout is only
// read on the platform that wrote it (as before).
// ---------------------------------------------------------------------------

// Layout written by versions before world position switched float -> double.
struct __attribute__( ( packed ) ) PlayerDataLegacy {
    int reserved;
    float world_x;
    float world_y;
    float world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
};

// Layout written before world_time (day/night clock) was appended.
struct __attribute__( ( packed ) ) PlayerDataNoTime {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
};

// Layout written before render_distance was appended (still has world_time).
struct __attribute__( ( packed ) ) PlayerDataNoDistance {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    long world_time;
};

// Layout written before min_ambient was appended (still has render_distance).
struct __attribute__( ( packed ) ) PlayerDataNoAmbient {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    long world_time;
    int render_distance;
};

// The last binary layout: PlayerDataNoAmbient + float min_ambient.
struct __attribute__( ( packed ) ) PlayerDataBinary {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    long world_time;
    int render_distance;
    float min_ambient;
};

PlayerData::PlayerData( ) {
    for ( InventorySlot &slot : this->hotbar_inventory ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    for ( InventorySlot &slot : this->survival_inventory ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
}

// Shared prefix of every binary layout — pos/angles/flags/quality, both
// inventories, and the hotbar selection + game mode at the tail.
template <typename L> static void legacy_to_player_data( const L &legacy, PlayerData &out ) {
    out.world_x = legacy.world_x;
    out.world_y = legacy.world_y;
    out.world_z = legacy.world_z;
    out.angle_H = legacy.angle_H;
    out.angle_V = legacy.angle_V;
    out.flying = legacy.flying;
    out.no_clip = legacy.no_clip;
    out.worldDrawQuality = legacy.worldDrawQuality;
    memcpy( out.hotbar_inventory, legacy.hotbar_inventory, sizeof( legacy.hotbar_inventory ) );
    out.selected_hotbar_slot = legacy.selected_hotbar_slot;
    out.game_mode = legacy.game_mode;
    memcpy( out.survival_inventory, legacy.survival_inventory, sizeof( legacy.survival_inventory ) );
}

// ---------------------------------------------------------------------------
// Text writer
// ---------------------------------------------------------------------------

static void write_slots( std::string &out, const char *key, const InventorySlot *slots, int count ) {
    char line[ 128 ];
    for ( int i = 0; i < count; i++ ) {
        if ( slots[ i ].item_id == LAST_BLOCK_ID || slots[ i ].quantity <= 0 ) {
            continue;
        }
        snprintf( line, sizeof( line ), "%s %d %u %d\n", key, i, slots[ i ].item_id, slots[ i ].quantity );
        out += line;
    }
}

std::string player_data_serialize( const PlayerData &data ) {
    std::string out;
    out.reserve( 1024 );
    char line[ 128 ];
    out += "# RepGame player data\n";
    out += "format 1\n";
    snprintf( line, sizeof( line ), "pos %.17g %.17g %.17g\n", data.world_x, data.world_y, data.world_z );
    out += line;
    snprintf( line, sizeof( line ), "angles %.9g %.9g\n", data.angle_H, data.angle_V );
    out += line;
    snprintf( line, sizeof( line ), "flying %d\n", data.flying ? 1 : 0 );
    out += line;
    snprintf( line, sizeof( line ), "no_clip %d\n", data.no_clip ? 1 : 0 );
    out += line;
    snprintf( line, sizeof( line ), "draw_quality %d\n", data.worldDrawQuality );
    out += line;
    snprintf( line, sizeof( line ), "shadow_quality %d\n", data.shadow_quality );
    out += line;
    snprintf( line, sizeof( line ), "game_mode %d\n", static_cast<int>( data.game_mode ) );
    out += line;
    snprintf( line, sizeof( line ), "peaceful %d\n", data.peaceful ? 1 : 0 );
    out += line;
    snprintf( line, sizeof( line ), "world_time %lld\n", static_cast<long long>( data.world_time ) );
    out += line;
    snprintf( line, sizeof( line ), "render_distance %d\n", data.render_distance );
    out += line;
    snprintf( line, sizeof( line ), "min_ambient %.9g\n", data.min_ambient );
    out += line;
    snprintf( line, sizeof( line ), "hotbar_selected %d\n", data.selected_hotbar_slot );
    out += line;
    write_slots( out, "hotbar", data.hotbar_inventory, HOTBAR_WIDTH * HOTBAR_HEIGHT );
    write_slots( out, "survival", data.survival_inventory, SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT );
    return out;
}

// ---------------------------------------------------------------------------
// Text reader
// ---------------------------------------------------------------------------

static bool parse_int( const char *s, const char **end, long long &value ) {
    char *next = nullptr;
    const long long v = strtoll( s, &next, 10 );
    if ( next == s ) {
        return false;
    }
    *end = next;
    value = v;
    return true;
}

static bool parse_double( const char *s, const char **end, double &value ) {
    char *next = nullptr;
    const double v = strtod( s, &next );
    if ( next == s ) {
        return false;
    }
    *end = next;
    value = v;
    return true;
}

static bool parse_slot_line( const char *s, InventorySlot *slots, int count ) {
    long long slot = -1;
    if ( !parse_int( s, &s, slot ) ) {
        return false;
    }
    long long item_id = -1, quantity = 0;
    if ( !parse_int( s, &s, item_id ) || !parse_int( s, &s, quantity ) ) {
        return false;
    }
    if ( slot < 0 || slot >= count ) {
        return true; // Out-of-range slot index: ignore the line, not the file.
    }
    slots[ slot ].item_id = static_cast<uint32_t>( item_id );
    slots[ slot ].quantity = static_cast<int>( quantity );
    return true;
}

// Parses the text format. `out` keeps defaults for keys the file lacks.
static bool player_data_parse_text( const char *buf, size_t len, PlayerData &out ) {
    bool have_format = false;
    bool have_pos = false;
    bool non_comment_seen = false;
    size_t i = 0;
    while ( i < len ) {
        // One line at a time; trim leading whitespace.
        while ( i < len && ( buf[ i ] == ' ' || buf[ i ] == '\t' || buf[ i ] == '\r' || buf[ i ] == '\n' ) ) {
            i++;
        }
        size_t line_end = i;
        while ( line_end < len && buf[ line_end ] != '\n' ) {
            line_end++;
        }
        const size_t line_len = line_end - i;
        if ( line_len == 0 || buf[ i ] == '#' ) {
            i = line_end;
            continue;
        }
        char line[ 256 ];
        if ( line_len >= sizeof( line ) ) {
            i = line_end;
            continue; // Way too long for a real field line: treat as garbage.
        }
        memcpy( line, buf + i, line_len );
        line[ line_len ] = '\0';
        i = line_end;

        if ( !non_comment_seen ) {
            // The first meaningful line decides the format — anything else
            // means this is not a text save at all.
            non_comment_seen = true;
            if ( strncmp( line, "format", 6 ) != 0 ) {
                return false;
            }
        }
        const char *s = line;
        const char *key_end = s;
        while ( *key_end && *key_end != ' ' && *key_end != '\t' ) {
            key_end++;
        }
        const size_t key_len = key_end - s;
        const char *v = key_end;
        while ( *v == ' ' || *v == '\t' ) {
            v++;
        }
        long long iv = 0;
        if ( key_len == 6 && memcmp( s, "format", 6 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                // Keys carry the compatibility: newer minor formats may add
                // keys this reader ignores, so accept any version >= 1.
                have_format = iv >= 1;
            }
        } else if ( key_len == 3 && memcmp( s, "pos", 3 ) == 0 ) {
            double x = 0.0, y = 0.0, z = 0.0;
            if ( parse_double( v, &v, x ) && parse_double( v, &v, y ) && parse_double( v, &v, z ) ) {
                out.world_x = x;
                out.world_y = y;
                out.world_z = z;
                have_pos = true;
            }
        } else if ( key_len == 6 && memcmp( s, "angles", 6 ) == 0 ) {
            double h = 0.0, a = 0.0;
            if ( parse_double( v, &v, h ) && parse_double( v, &v, a ) ) {
                out.angle_H = static_cast<float>( h );
                out.angle_V = static_cast<float>( a );
            }
        } else if ( key_len == 6 && memcmp( s, "flying", 6 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.flying = iv != 0;
            }
        } else if ( key_len == 7 && memcmp( s, "no_clip", 7 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.no_clip = iv != 0;
            }
        } else if ( key_len == 12 && memcmp( s, "draw_quality", 12 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.worldDrawQuality = static_cast<int>( iv );
            }
        } else if ( key_len == 14 && memcmp( s, "shadow_quality", 14 ) == 0 ) {
            if ( parse_int( v, &v, iv ) && iv >= 0 && iv <= 1 ) {
                out.shadow_quality = static_cast<int>( iv );
            }
        } else if ( key_len == 9 && memcmp( s, "game_mode", 9 ) == 0 ) {
            if ( parse_int( v, &v, iv ) && iv >= 0 && iv <= GameMode_Survival ) {
                out.game_mode = static_cast<GameMode>( iv );
            }
        } else if ( key_len == 8 && memcmp( s, "peaceful", 8 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.peaceful = iv != 0;
            }
        } else if ( key_len == 10 && memcmp( s, "world_time", 10 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.world_time = static_cast<int64_t>( iv );
            }
        } else if ( key_len == 15 && memcmp( s, "render_distance", 15 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.render_distance = static_cast<int>( iv );
            }
        } else if ( key_len == 11 && memcmp( s, "min_ambient", 11 ) == 0 ) {
            double a = 0.0;
            if ( parse_double( v, &v, a ) ) {
                out.min_ambient = static_cast<float>( a );
            }
        } else if ( key_len == 15 && memcmp( s, "hotbar_selected", 15 ) == 0 ) {
            if ( parse_int( v, &v, iv ) ) {
                out.selected_hotbar_slot = static_cast<int>( iv );
            }
        } else if ( key_len == 6 && memcmp( s, "hotbar", 6 ) == 0 ) {
            parse_slot_line( v, out.hotbar_inventory, HOTBAR_WIDTH * HOTBAR_HEIGHT );
        } else if ( key_len == 8 && memcmp( s, "survival", 8 ) == 0 ) {
            parse_slot_line( v, out.survival_inventory, SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT );
        } else {
            pr_debug( "player.dat: ignoring unknown key %.*s", static_cast<int>( key_len ), s );
        }
    }
    return have_format && have_pos;
}

// ---------------------------------------------------------------------------
// Dispatch: text first, then legacy binary layouts by exact size.
// ---------------------------------------------------------------------------

bool player_data_parse( const unsigned char *buf, size_t len, PlayerData &out ) {
    // Cheap text check up front: only the text path needs len > sizeof(bin).
    {
        PlayerData text;
        if ( player_data_parse_text( reinterpret_cast<const char *>( buf ), len, text ) ) {
            out = text;
            return true;
        }
        // If it looked like text (a `format` line) but failed, don't fall
        // through to binary — a corrupt text save must not hit the size
        // branches by accident.
        const char *s = reinterpret_cast<const char *>( buf );
        size_t i = 0;
        while ( i < len ) {
            if ( s[ i ] == '#' ) {
                while ( i < len && s[ i ] != '\n' ) {
                    i++;
                }
            } else if ( s[ i ] == ' ' || s[ i ] == '\t' || s[ i ] == '\r' || s[ i ] == '\n' ) {
                i++;
            } else {
                break;
            }
        }
        if ( i + 6 <= len && memcmp( s + i, "format", 6 ) == 0 ) {
            return false;
        }
    }

    if ( len == sizeof( PlayerDataBinary ) ) {
        PlayerDataBinary legacy;
        memcpy( &legacy, buf, sizeof( legacy ) );
        PlayerData parsed;
        legacy_to_player_data( legacy, parsed );
        parsed.world_time = legacy.world_time;
        parsed.render_distance = legacy.render_distance;
        parsed.min_ambient = legacy.min_ambient;
        out = parsed;
        return true;
    }
    if ( len == sizeof( PlayerDataNoAmbient ) ) {
        // render_distance present, min_ambient absent -> keeps the -1 default.
        PlayerDataNoAmbient legacy;
        memcpy( &legacy, buf, sizeof( legacy ) );
        PlayerData parsed;
        legacy_to_player_data( legacy, parsed );
        parsed.world_time = legacy.world_time;
        parsed.render_distance = legacy.render_distance;
        out = parsed;
        return true;
    }
    if ( len == sizeof( PlayerDataNoDistance ) ) {
        // world_time present; render_distance stays 0 (absent).
        PlayerDataNoDistance legacy;
        memcpy( &legacy, buf, sizeof( legacy ) );
        PlayerData parsed;
        legacy_to_player_data( legacy, parsed );
        parsed.world_time = legacy.world_time;
        out = parsed;
        return true;
    }
    if ( len == sizeof( PlayerDataNoTime ) ) {
        PlayerDataNoTime legacy;
        memcpy( &legacy, buf, sizeof( legacy ) );
        PlayerData parsed;
        legacy_to_player_data( legacy, parsed );
        out = parsed;
        return true;
    }
    if ( len == sizeof( PlayerDataLegacy ) ) {
        PlayerDataLegacy legacy;
        memcpy( &legacy, buf, sizeof( legacy ) );
        PlayerData parsed;
        legacy_to_player_data( legacy, parsed );
        out = parsed;
        return true;
    }
    return false;
}
