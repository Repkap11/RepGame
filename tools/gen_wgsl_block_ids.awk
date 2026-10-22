#!/usr/bin/awk -f
# Generates the WGSL BlockID prelude for the WebGPU mapgen kernel:
# reads the `typedef enum { ... } BlockID;` in block_definitions.hpp and emits
#   const AIR : u32 = 0;
#   const GRASS : u32 = 1;
#   ...
# so the enum stays the single source of truth — adding/reordering block IDs
# never requires a hand-edited WGSL copy. Handles implicit (++) and explicit
# (= N) values, line comments, and multiple members per line. Duplicate
# VALUES are fine (aliases like REDSTONE_LAMP); duplicate names would be a
# WGSL error just as they'd be a C++ error.
BEGIN { in_enum = 0; val = 0 }
/typedef[ \t]+enum[ \t]*{/ { in_enum = 1; next }
in_enum && /}/ { in_enum = 0; exit }
in_enum {
    sub( /\/\/.*/, "" )          # strip // comments
    n = split( $0, parts, "," )
    for ( i = 1; i <= n; i++ ) {
        tok = parts[i]
        gsub( /^[ \t]+|[ \t]+$/, "", tok )
        if ( tok == "" )
            continue
        if ( index( tok, "=" ) ) {
            split( tok, kv, "=" )
            name = kv[1]; gsub( /[ \t]/, "", name )
            val = kv[2] + 0
        } else {
            name = tok; gsub( /[ \t]/, "", name )
        }
        print "const " name " : u32 = " val ";"
        val++
    }
}
