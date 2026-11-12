// Sign text helpers — see include/common/sign.hpp.

#include "common/sign.hpp"

bool block_is_sign( BlockID id ) {
    switch ( id ) {
        case STANDING_SIGN:
        case ACACIA_STANDING_SIGN:
        case BAMBOO_STANDING_SIGN:
        case BIRCH_STANDING_SIGN:
        case CHERRY_STANDING_SIGN:
        case CRIMSON_STANDING_SIGN:
        case DARKOAK_STANDING_SIGN:
        case JUNGLE_STANDING_SIGN:
        case MANGROVE_STANDING_SIGN:
        case PALE_OAK_STANDING_SIGN:
        case POPLAR_STANDING_SIGN:
        case SPRUCE_STANDING_SIGN:
        case WARPED_STANDING_SIGN:
        case OAK_HANGING_SIGN:
        case ACACIA_HANGING_SIGN:
        case BAMBOO_HANGING_SIGN:
        case BIRCH_HANGING_SIGN:
        case CHERRY_HANGING_SIGN:
        case CRIMSON_HANGING_SIGN:
        case DARK_OAK_HANGING_SIGN:
        case JUNGLE_HANGING_SIGN:
        case MANGROVE_HANGING_SIGN:
        case PALE_OAK_HANGING_SIGN:
        case POPLAR_HANGING_SIGN:
        case SPRUCE_HANGING_SIGN:
        case WARPED_HANGING_SIGN:
        case WALL_SIGN:
        case ACACIA_WALL_SIGN:
        case BAMBOO_WALL_SIGN:
        case BIRCH_WALL_SIGN:
        case CHERRY_WALL_SIGN:
        case CRIMSON_WALL_SIGN:
        case DARKOAK_WALL_SIGN:
        case JUNGLE_WALL_SIGN:
        case MANGROVE_WALL_SIGN:
        case PALE_OAK_WALL_SIGN:
        case POPLAR_WALL_SIGN:
        case SPRUCE_WALL_SIGN:
        case WARPED_WALL_SIGN:
            return true;
        default:
            return false;
    }
}

bool block_is_wall_sign( BlockID id ) {
    switch ( id ) {
        case WALL_SIGN:
        case ACACIA_WALL_SIGN:
        case BAMBOO_WALL_SIGN:
        case BIRCH_WALL_SIGN:
        case CHERRY_WALL_SIGN:
        case CRIMSON_WALL_SIGN:
        case DARKOAK_WALL_SIGN:
        case JUNGLE_WALL_SIGN:
        case MANGROVE_WALL_SIGN:
        case PALE_OAK_WALL_SIGN:
        case POPLAR_WALL_SIGN:
        case SPRUCE_WALL_SIGN:
        case WARPED_WALL_SIGN:
            return true;
        default:
            return false;
    }
}

bool block_is_hanging_sign( BlockID id ) {
    switch ( id ) {
        case OAK_HANGING_SIGN:
        case ACACIA_HANGING_SIGN:
        case BAMBOO_HANGING_SIGN:
        case BIRCH_HANGING_SIGN:
        case CHERRY_HANGING_SIGN:
        case CRIMSON_HANGING_SIGN:
        case DARK_OAK_HANGING_SIGN:
        case JUNGLE_HANGING_SIGN:
        case MANGROVE_HANGING_SIGN:
        case PALE_OAK_HANGING_SIGN:
        case POPLAR_HANGING_SIGN:
        case SPRUCE_HANGING_SIGN:
        case WARPED_HANGING_SIGN:
            return true;
        default:
            return false;
    }
}

void sign_sanitize_line( std::string &line ) {
    size_t out = 0;
    for ( size_t i = 0; i < line.size( ) && out < SIGN_LINE_MAX; i++ ) {
        const unsigned char c = static_cast<unsigned char>( line[ i ] );
        if ( c >= 32 && c <= 126 ) {
            line[ out++ ] = static_cast<char>( c );
        }
    }
    line.resize( out );
}

void sign_sanitize( SignText &text ) {
    for ( std::string &line : text.lines ) {
        sign_sanitize_line( line );
    }
}
