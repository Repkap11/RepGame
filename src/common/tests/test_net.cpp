// Multiplayer entity-sync tests — split out of rep_tests.cpp.
#include "test_util.hpp"

// Codec-registry coverage: every NetMsgType has a table row (contiguous, in
// wire-id order), every row has a name, and every payload-bearing row
// round-trips its deterministic sample and rejects a half-length buffer.
// Adding a message type without a row fails the contiguity check.
int test_net_codec_registry( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    size_t count = 0;
    const NetCodecEntry *table = net_codec_table( &count );
    bool contiguous = count == static_cast<size_t>( NET_MSG_TYPE_COUNT );
    if ( contiguous ) {
        for ( size_t i = 0; i < count; i++ ) {
            if ( table[ i ].type != static_cast<NetMsgType>( i ) ) {
                contiguous = false;
                break;
            }
        }
    }
    report( "codec registry: one contiguous row per wire id", contiguous );

    char namebuf[ 96 ];
    for ( size_t i = 0; contiguous && i < count; i++ ) {
        const NetCodecEntry &e = table[ i ];
        snprintf( namebuf, sizeof( namebuf ), "codec registry: %s named", e.name );
        report( namebuf, e.name != nullptr && net_msg_type_name( e.type ) == e.name );
        if ( e.ser == nullptr ) {
            // Payloadless or retired wire id.
            snprintf( namebuf, sizeof( namebuf ), "codec registry: %s has no codec", e.name );
            report( namebuf, e.make == nullptr && e.de == nullptr );
            continue;
        }
        // fill -> ser -> de -> ser must reproduce identical bytes.
        void *sample = e.make( );
        PacketWriter w1;
        e.ser( w1, sample );
        void *decoded = e.make( );
        PacketReader r( w1.buf( ).data( ), w1.buf( ).size( ) );
        const bool decoded_ok = e.de( r, decoded );
        PacketWriter w2;
        if ( decoded_ok ) {
            e.ser( w2, decoded );
        }
        snprintf( namebuf, sizeof( namebuf ), "codec registry: %s round trip", e.name );
        report( namebuf, decoded_ok && w2.buf( ) == w1.buf( ) );
        // A half-length buffer must be rejected (reads are bounds-checked).
        void *junk = e.make( );
        PacketReader rt( w1.buf( ).data( ), w1.buf( ).size( ) / 2 );
        snprintf( namebuf, sizeof( namebuf ), "codec registry: %s truncated fails", e.name );
        report( namebuf, !e.de( rt, junk ) );
        e.destroy( sample );
        e.destroy( decoded );
        e.destroy( junk );
    }
    report( "codec registry: out-of-range id unnamed",
            strcmp( net_msg_type_name( static_cast<NetMsgType>( 200 ) ), "UNKNOWN_MSG" ) == 0 );
    return failures;
}

// Container wire family: kind-def lookups + arity, block->kind routing,
// CONTAINER_POS kind validation, container_clean_slot normalization, and the
// blob<->typed-view converters that bridge wire and chunk storage.
int test_net_containers( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    // Kind table: every enum value resolves, out-of-range is rejected.
    const NetContainerKindDef *cd = net_container_kind_def( static_cast<uint8_t>( NetContainerKind::CHEST ) );
    const NetContainerKindDef *fd = net_container_kind_def( static_cast<uint8_t>( NetContainerKind::FURNACE ) );
    report( "container kind: chest def resolves", cd != nullptr && cd->kind == NetContainerKind::CHEST );
    report( "container kind: furnace def resolves", fd != nullptr && fd->kind == NetContainerKind::FURNACE );
    report( "container kind: out of range rejected",
            net_container_kind_def( NET_CONTAINER_KIND_COUNT ) == nullptr && net_container_kind_def( 200 ) == nullptr );
    report( "container kind: chest arity 27/0", cd != nullptr && cd->slots == CHEST_SLOTS && cd->aux == 0 );
    report( "container kind: furnace arity 3/2 + hooks",
            fd != nullptr && fd->slots == FURNACE_INVENTORY_SLOTS && fd->aux == 2 && fd->tick != nullptr && fd->active != nullptr && fd->lit != nullptr );
    report( "container kind: furnace lit pair", fd != nullptr && fd->unlit_block == FURNACE && fd->lit_block == LIT_FURNACE );
    // Block routing: both furnace ids land on one kind; others reject.
    report( "container kind: CHEST block routes", net_container_kind_of_block( CHEST ) == cd );
    report( "container kind: FURNACE block routes", net_container_kind_of_block( FURNACE ) == fd );
    report( "container kind: LIT_FURNACE routes same", net_container_kind_of_block( LIT_FURNACE ) == fd );
    report( "container kind: non-container rejected",
            net_container_kind_of_block( STONE ) == nullptr && net_container_kind_of_block( AIR ) == nullptr );

    // CONTAINER_POS round trip + kind-byte validation.
    {
        NetContainerPosPayload out{ NetContainerKind::FURNACE, -4, 70, 900 };
        PacketWriter w;
        net_serialize_container_pos( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetContainerPosPayload in{};
        report( "container pos: round trip",
                net_deserialize_container_pos( r, in ) && in.kind == NetContainerKind::FURNACE && in.x == -4 && in.y == 70 && in.z == 900 );
    }
    { // An unknown kind byte fails the frame outright.
        PacketWriter w;
        w.write_u8( 99 );
        w.write_i32( 1 );
        w.write_i32( 2 );
        w.write_i32( 3 );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetContainerPosPayload in{};
        report( "container pos: bad kind rejected", !net_deserialize_container_pos( r, in ) );
    }

    // container_clean_slot: invalid -> empty, over-stack -> clamped.
    {
        InventorySlot c = container_clean_slot( InventorySlot{ ITEM_ID_END, 4 } );
        report( "container clean: invalid id empties", c.item_id == LAST_BLOCK_ID && c.quantity == 0 );
        c = container_clean_slot( InventorySlot{ STONE, -3 } );
        report( "container clean: non-positive empties", c.item_id == LAST_BLOCK_ID && c.quantity == 0 );
        c = container_clean_slot( InventorySlot{ STICK, 999 } );
        report( "container clean: over-stack clamps", c.item_id == STICK && c.quantity == item_max_stack( STICK ) );
        c = container_clean_slot( InventorySlot{ STICK, 7 } );
        report( "container clean: valid kept", c.item_id == STICK && c.quantity == 7 );
    }

    // Blob converters: typed views <-> interchange shape round-trip; a
    // short/foreign blob pads defensively rather than reading OOB.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 3 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        f.cook_progress = 700;
        f.burn_ticks = 5000;
        const ContainerContents blob = container_to_blob( f );
        report( "container blob: furnace aux carries progress",
                blob.slots.size( ) == FURNACE_INVENTORY_SLOTS && blob.aux.size( ) == 2 && blob.aux[ 0 ] == 700 && blob.aux[ 1 ] == 5000 );
        FurnaceContents back;
        container_from_blob( blob, back );
        report( "container blob: furnace round trip",
                back.slots[ 0 ].item_id == RAW_IRON && back.slots[ 0 ].quantity == 3 && back.cook_progress == 700 && back.burn_ticks == 5000 );
        ContainerContents short_blob;
        short_blob.slots.assign( 1, InventorySlot{ STONE, 2 } );
        FurnaceContents padded;
        container_from_blob( short_blob, padded );
        report( "container blob: short blob pads",
                padded.slots[ 0 ].item_id == STONE && padded.slots[ 1 ].item_id == LAST_BLOCK_ID && padded.cook_progress == 0 && padded.burn_ticks == 0 );
        ChestContents chest;
        chest.slots[ 5 ] = { GOLD_BLOCK, 12 };
        const ContainerContents cblob = container_to_blob( chest );
        ChestContents cback;
        container_from_blob( cblob, cback );
        report( "container blob: chest round trip",
                cblob.slots.size( ) == CHEST_SLOTS && cblob.aux.empty( ) && cback.slots[ 5 ].item_id == GOLD_BLOCK && cback.slots[ 5 ].quantity == 12 );
    }
    return failures;
}

// Round-trip every entity-sync payload (protocol v5): serialize then
// deserialize must reproduce the input, and a truncated buffer must fail.
int test_net_entities( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    { // WELCOME
        NetWelcomePayload out{ 7 };
        PacketWriter w;
        net_serialize_welcome( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetWelcomePayload in{ 0 };
        report( "net entities: WELCOME round trip", net_deserialize_welcome( r, in ) && in.session_id == 7 );
    }
    { // VIEW
        NetViewPayload out{ -3, 2, 40, 5, 2 };
        PacketWriter w;
        net_serialize_view( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetViewPayload in{ 0, 0, 0, 0, 0 };
        report( "net entities: VIEW round trip", net_deserialize_view( r, in ) && in.cx == -3 && in.cy == 2 && in.cz == 40 && in.radius_xz == 5 && in.radius_y == 2 );
    }

    // One fully-populated record reused by the record-bearing messages.
    NetEntityRecord rec;
    rec.id = ( static_cast<NetEntityId>( 3 ) << 32 ) | 17;
    rec.kind = NetEntityKind::BODY;
    rec.x = -12.5;
    rec.y = 70.25;
    rec.z = 1e9 + 0.5;
    rec.vx = 0.1f;
    rec.vy = -0.2f;
    rec.vz = 0.3f;
    rec.yaw = 1.25f;
    rec.type = 2;
    rec.count = -4;
    rec.health = 13.5f;
    rec.age_ticks = 12345;
    rec.pickup_delay = 8;
    rec.flags = 0xdeadbeef;
    rec.block_state = { TNT, 3, 7, TNT, 1 };
    rec.fuse_ticks = 60;
    const auto record_eq = [ & ]( const NetEntityRecord &a, const NetEntityRecord &b ) {
        return a.id == b.id && a.kind == b.kind && a.x == b.x && a.y == b.y && a.z == b.z &&             //
               a.vx == b.vx && a.vy == b.vy && a.vz == b.vz && a.yaw == b.yaw && a.type == b.type &&   //
               a.count == b.count && a.health == b.health && a.age_ticks == b.age_ticks &&           //
               a.pickup_delay == b.pickup_delay && a.flags == b.flags && a.fuse_ticks == b.fuse_ticks && //
               BlockStates_equal( a.block_state, b.block_state );
    };
    { // ENTITY_SPAWN / record round trip
        PacketWriter w;
        net_write_entity_record( w, rec );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRecord in;
        report( "net entities: ENTITY_SPAWN record round trip", net_read_entity_record( r, in ) && record_eq( rec, in ) );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetEntityRecord junk;
        report( "net entities: record truncated fails", !net_read_entity_record( rt, junk ) );
    }
    { // CHUNK_ROLE with two records
        NetChunkRolePayload out;
        out.cx = 1;
        out.cy = -2;
        out.cz = 3;
        out.role = NET_ROLE_OWNER;
        out.populated = 1;
        out.records.push_back( rec );
        NetEntityRecord rec2 = rec;
        rec2.id = 99;
        rec2.kind = NetEntityKind::ITEM;
        out.records.push_back( rec2 );
        out.removed_natural = { 0, 5, 63 };
        PacketWriter w;
        net_serialize_chunk_role( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetChunkRolePayload in;
        report( "net entities: CHUNK_ROLE round trip",
                net_deserialize_chunk_role( r, in ) && in.cx == 1 && in.cy == -2 && in.cz == 3 && in.role == NET_ROLE_OWNER && in.populated == 1 &&
                    in.records.size( ) == 2 && record_eq( in.records[ 0 ], rec ) && record_eq( in.records[ 1 ], rec2 ) &&
                    in.removed_natural == out.removed_natural );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetChunkRolePayload junk;
        report( "net entities: CHUNK_ROLE truncated fails", !net_deserialize_chunk_role( rt, junk ) );
        // Over-cap list is rejected.
        PacketWriter w2;
        w2.write_i32( 0 );
        w2.write_i32( 0 );
        w2.write_i32( 0 );
        w2.write_u8( NET_ROLE_VIEWER );
        w2.write_u8( 0 );
        w2.write_u32( NET_ENTITY_MAX_LIST + 1 );
        PacketReader r2( w2.buf( ).data( ), w2.buf( ).size( ) );
        NetChunkRolePayload junk2;
        report( "net entities: CHUNK_ROLE over-cap fails", !net_deserialize_chunk_role( r2, junk2 ) );
    }
    { // ENTITY_STATE batch
        NetEntityStatePayload out;
        NetEntityStateEntry e;
        e.id = 42;
        e.kind = static_cast<uint8_t>( NetEntityKind::ITEM );
        e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW | NET_SE_COUNT | NET_SE_HEALTH | NET_SE_FLAGS;
        e.x = 1.5;
        e.y = -2.5;
        e.z = 3.5;
        e.vx = 0.01f;
        e.vy = -0.02f;
        e.vz = 0.03f;
        e.yaw = 0.5f;
        e.count = 7;
        e.health = 4.5f;
        e.flags = 9;
        out.entries.push_back( e );
        out.entries.push_back( e );
        out.entries[ 1 ].id = 43;
        PacketWriter w;
        net_serialize_entity_state( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityStatePayload in;
        const NetEntityStateEntry &g = in.entries.size( ) == 2 ? in.entries[ 0 ] : e;
        // ABS pos is exact f64; vel quantizes to 1/4096 and yaw to 360/256.
        report( "net entities: ENTITY_STATE round trip",
                net_deserialize_entity_state( r, in ) && in.entries.size( ) == 2 && g.id == 42 && g.kind == static_cast<uint8_t>( NetEntityKind::ITEM ) &&
                    g.x == 1.5 && g.y == -2.5 && g.z == 3.5 && fabsf( g.vx - 0.01f ) < 1e-3f && fabsf( g.vy + 0.02f ) < 1e-3f &&
                    fabsf( g.vz - 0.03f ) < 1e-3f && fabsf( g.yaw - 0.5f ) < 1.5f && g.count == 7 && g.health == 4.5f && g.flags == 9 );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetEntityStatePayload junk;
        report( "net entities: ENTITY_STATE truncated fails", !net_deserialize_entity_state( rt, junk ) );
    }
    { // ENTITY_REMOVE
        NetEntityRemovePayload out{ 0xABCDEF0123456789ull, NET_REMOVE_MERGED };
        PacketWriter w;
        net_serialize_entity_remove( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRemovePayload in{ 0, 0 };
        report( "net entities: ENTITY_REMOVE round trip", net_deserialize_entity_remove( r, in ) && in.id == out.id && in.reason == NET_REMOVE_MERGED );
    }
    { // ENTITY_TRANSFER
        NetEntityTransferPayload out{ rec, 1 };
        PacketWriter w;
        net_serialize_entity_transfer( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityTransferPayload in;
        report( "net entities: ENTITY_TRANSFER round trip", net_deserialize_entity_transfer( r, in ) && in.adopt == 1 && record_eq( in.record, rec ) );
    }
    { // ENTITY_ACTION
        NetEntityActionPayload out{ 77, NET_ACTION_HIT, 4.5f, 1.0, 2.0, 3.0, 1, 0 };
        PacketWriter w;
        net_serialize_entity_action( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityActionPayload in{ 0, 0, 0.0f, 0.0, 0.0, 0.0, 0, 0 };
        report( "net entities: ENTITY_ACTION round trip",
                net_deserialize_entity_action( r, in ) && in.id == 77 && in.action == NET_ACTION_HIT && in.damage == 4.5f && in.ax == 1.0 && in.ay == 2.0 && in.az == 3.0 && in.attacker_survival == 1 && in.item == 0 );
    }
    { // ENTITY_ACTION FEED: the breed item the client claims to hold.
        NetEntityActionPayload out{ 88, NET_ACTION_FEED, 0.0f, 1.0, 2.0, 3.0, 1, WHEAT };
        PacketWriter w;
        net_serialize_entity_action( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityActionPayload in{ 0, 0, 0.0f, 0.0, 0.0, 0.0, 0, 0 };
        report( "net entities: ENTITY_ACTION FEED round trip",
                net_deserialize_entity_action( r, in ) && in.id == 88 && in.action == NET_ACTION_FEED && in.item == WHEAT );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetEntityActionPayload junk{ 0, 0, 0.0f, 0.0, 0.0, 0.0, 0, 0 };
        report( "net entities: ENTITY_ACTION truncated fails", !net_deserialize_entity_action( rt, junk ) );
    }
    { // PICKUP_REQUEST / PICKUP_RESULT
        NetPickupRequestPayload out{ 55, 12 };
        PacketWriter w;
        net_serialize_pickup_request( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPickupRequestPayload in{ 0, 0 };
        report( "net entities: PICKUP_REQUEST round trip", net_deserialize_pickup_request( r, in ) && in.id == 55 && in.room == 12 );

        NetPickupResultPayload out2{ 55, STICK, 12 };
        PacketWriter w2;
        net_serialize_pickup_result( w2, out2 );
        PacketReader r2( w2.buf( ).data( ), w2.buf( ).size( ) );
        NetPickupResultPayload in2{ 0, 0, 0 };
        report( "net entities: PICKUP_RESULT round trip", net_deserialize_pickup_result( r2, in2 ) && in2.id == 55 && in2.item == STICK && in2.granted == 12 );
    }
    { // PLAYER_HIT
        NetPlayerHitPayload out{ 6, -1.5, 2.5, -3.5, 3.25f };
        PacketWriter w;
        net_serialize_player_hit( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPlayerHitPayload in{ 0, 0.0, 0.0, 0.0, 0.0f };
        report( "net entities: PLAYER_HIT round trip",
                net_deserialize_player_hit( r, in ) && in.target_player_id == 6 && in.fx == -1.5 && in.fy == 2.5 && in.fz == -3.5 && in.damage == 3.25f );
    }
    { // EXPLOSION
        NetExplosionPayload out{ 10.5, 64.0, -20.25, 4.0f };
        PacketWriter w;
        net_serialize_explosion( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetExplosionPayload in{ 0.0, 0.0, 0.0, 0.0f };
        report( "net entities: EXPLOSION round trip", net_deserialize_explosion( r, in ) && in.x == 10.5 && in.y == 64.0 && in.z == -20.25 && in.radius == 4.0f );
    }
    { // NetPlayerPayload flags + health survive.
        NetPlayerPayload out;
        memset( &out, 0, sizeof( out ) );
        out.flags = NET_PLAYER_FLAG_TARGETABLE;
        out.health = 13.5f;
        PacketWriter w;
        net_serialize_player( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPlayerPayload in;
        memset( &in, 0, sizeof( in ) );
        report( "net entities: player flags+health round trip",
                net_deserialize_player( r, in ) && in.flags == NET_PLAYER_FLAG_TARGETABLE && in.health == 13.5f );
    }
    { // Trailing riding_vehicle (ridden cart net id) round trips, and an old
      // payload without it decodes to 0.
        NetPlayerPayload out;
        memset( &out, 0, sizeof( out ) );
        out.x = 4.0f;
        out.held_item = STICK;
        out.riding_vehicle = 0x1BADC0FFEEull;
        PacketWriter w;
        net_serialize_player( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPlayerPayload in;
        memset( &in, 0, sizeof( in ) );
        report( "net entities: player riding_vehicle round trip",
                net_deserialize_player( r, in ) && in.riding_vehicle == 0x1BADC0FFEEull );
        NetPlayerPayload old_out;
        memset( &old_out, 0, sizeof( old_out ) );
        old_out.x = 4.0f;
        PacketWriter wo;
        net_serialize_player( wo, old_out );
        std::vector<uint8_t> old_buf = wo.take_buf( );
        old_buf.resize( old_buf.size( ) - sizeof( uint64_t ) ); // drop the trailing field
        PacketReader ro( old_buf.data( ), old_buf.size( ) );
        NetPlayerPayload old_in;
        memset( &old_in, 0, sizeof( old_in ) );
        old_in.riding_vehicle = 7;
        report( "net entities: player absent riding_vehicle defaults 0",
                net_deserialize_player( ro, old_in ) && old_in.riding_vehicle == 0 && old_in.x == 4.0f );
    }
    { // CHUNK_POPULATED
        NetChunkPopulatedPayload out{ -3, 0, 7 };
        PacketWriter w;
        net_serialize_chunk_populated( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetChunkPopulatedPayload in{ 0, 0, 0 };
        report( "net entities: CHUNK_POPULATED round trip", net_deserialize_chunk_populated( r, in ) && in.cx == -3 && in.cy == 0 && in.cz == 7 );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetChunkPopulatedPayload junk{ 0, 0, 0 };
        report( "net entities: CHUNK_POPULATED truncated rejects", !net_deserialize_chunk_populated( rt, junk ) );
    }
    return failures;
}

// An owned synced item that drifts into a foreign-owned chunk must keep
// streaming ENTITY_STATE — ownership keys off net_chunk (the chunk the
// server believes it sits in), which only advances once a state is sent, so
// the server sees the crossing and issues ENTITY_TRANSFER. Before net_chunk
// the local pos was used and the crossing was stranded.
int test_net_item_crossing( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    Multiplayer mp;
    NetEntities net( &mp );
    ItemEntities items;
    items.set_multiplayer_mode( true, &net );

    const glm::ivec3 chunk_a( 0, 0, 0 ), chunk_b( 1, 0, 0 );
    net.test_set_role( chunk_a, NET_ROLE_OWNER );

    NetEntityRecord rec;
    rec.id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 1;
    rec.kind = NetEntityKind::ITEM;
    rec.x = 30.0;
    rec.y = 40.0;
    rec.z = 10.0;
    rec.type = DIRT;
    rec.count = 4;
    items.net_apply_spawn( rec, /*mirror*/ false );

    std::vector<NetEntityStateEntry> out;
    items.collect_net_state( net, out, 0 );
    report( "crossing: initial state emitted", out.size( ) == 1 );

    // Margin rest: 0.3 blocks inside chunk_b keeps net_chunk on chunk_a and
    // keeps streaming — the server's boundary hysteresis applies to the sent
    // pos, so a boundary-resting item is never stranded.
    items.test_items( )[ 0 ].pos = glm::dvec3( 62.3, 40.0, 10.0 );
    out.clear( );
    items.collect_net_state( net, out, 6 );
    report( "crossing: margin rest keeps net_chunk and streams",
            out.size( ) == 1 && items.test_items( )[ 0 ].net.chunk == chunk_a );

    // Drag the item into foreign chunk_b: the state still goes out because
    // net_chunk is still chunk_a — the server learns of the crossing.
    items.test_items( )[ 0 ].pos = glm::dvec3( 70.0, 40.0, 10.0 );
    out.clear( );
    items.collect_net_state( net, out, 12 );
    report( "crossing: state emitted while crossing", out.size( ) == 1 && out[ 0 ].x == 70.0 );
    report( "crossing: net_chunk advanced to sent pos", items.test_items( )[ 0 ].net.chunk == chunk_b );

    // Server-belief moved: without a role on chunk_b the item stops
    // streaming — the server now drives it via TRANSFER/adopt=0.
    items.test_items( )[ 0 ].pos.x += 1.0;
    out.clear( );
    items.collect_net_state( net, out, 18 );
    report( "crossing: stops streaming in foreign chunk", out.empty( ) );

    // ENTITY_TRANSFER adopt=0 while still viewed: becomes a mirror.
    items.net_release_entity( rec, /*still_viewed*/ true );
    report( "crossing: adopt=0 becomes mirror", items.test_items( )[ 0 ].net.mirror );

    return failures;
}

// A CHUNK_ROLE grant's record snapshot can predate an entity that entered
// the chunk after it was generated (own spawn echo, a remote spawn relay, a
// transfer): keep_ids spares those ids from the resync wipe and settles
// net.mirror to !owned — the mirror flag was guessed before the role was
// known. An unknown id is wiped as stale.
int test_net_item_grant( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    const glm::ivec3 chunk( 0, 0, 0 );
    const auto item_rec = [ ]( NetEntityId id ) {
        NetEntityRecord rec;
        rec.id = id;
        rec.kind = NetEntityKind::ITEM;
        rec.x = 30.0;
        rec.y = 40.0;
        rec.z = 10.0;
        rec.type = DIRT;
        rec.count = 4;
        return rec;
    };
    const std::vector<NetEntityRecord> empty_records;

    // A kept id survives an empty OWNER snapshot; a mirror flag guessed
    // pre-role settles to owned so local simulation resumes.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        ItemEntities items;
        items.set_multiplayer_mode( true, &net );
        const NetEntityId id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 1;
        items.net_apply_spawn( item_rec( id ), /*mirror*/ true );
        items.net_apply_chunk_role( chunk, NET_ROLE_OWNER, empty_records, { id } );
        report( "grant: kept id survives empty OWNER snapshot", items.debug_items( ).size( ) == 1 );
        report( "grant: kept id settles to owned", !items.debug_items( )[ 0 ].net.mirror );
    }
    // The same keep under VIEWER settles to mirror.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        ItemEntities items;
        items.set_multiplayer_mode( true, &net );
        const NetEntityId id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 2;
        items.net_apply_spawn( item_rec( id ), /*mirror*/ false );
        items.net_apply_chunk_role( chunk, NET_ROLE_VIEWER, empty_records, { id } );
        report( "grant: kept id settles to mirror under VIEWER",
                items.debug_items( ).size( ) == 1 && items.debug_items( )[ 0 ].net.mirror );
    }
    // An id the snapshot doesn't carry and keep_ids doesn't spare is wiped.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        ItemEntities items;
        items.set_multiplayer_mode( true, &net );
        const NetEntityId id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 3;
        items.net_apply_spawn( item_rec( id ), /*mirror*/ false );
        items.net_apply_chunk_role( chunk, NET_ROLE_OWNER, empty_records, {} );
        report( "grant: unknown id wiped by resync", items.debug_items( ).empty( ) );
    }
    // Our own in-view spawn survives a stale snapshot WITHOUT keep_ids: the
    // server has it live, absence just means the snapshot predates it —
    // this is the in-flight grant race, and it must hold even without the
    // server's spawn echo (old servers).
    {
        Multiplayer mp;
        NetEntities net( &mp );
        net.test_welcome( 9 );
        net.test_view( chunk, 8, 3 );
        ItemEntities items;
        items.set_multiplayer_mode( true, &net );
        const NetEntityId id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 4;
        const NetEntityRecord rec = item_rec( id );
        net.send_spawn( rec ); // in-view: not a stray
        items.net_apply_spawn( rec, /*mirror*/ false );
        items.net_apply_chunk_role( chunk, NET_ROLE_OWNER, empty_records, {} );
        report( "grant: own in-view spawn survives stale snapshot", items.debug_items( ).size( ) == 1 );
        report( "grant: own spawn settles to owned", !items.debug_items( ).empty( ) && !items.debug_items( )[ 0 ].net.mirror );
    }
    // But a spawn outside our view is a stray: the server sidecar-persists
    // it under no id, so the wipe must still run — the grant's remapped
    // record is its replacement.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        net.test_welcome( 9 ); // no view sent: every spawn chunk is outside
        ItemEntities items;
        items.set_multiplayer_mode( true, &net );
        const NetEntityId id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 5;
        const NetEntityRecord rec = item_rec( id );
        net.send_spawn( rec ); // marks the id as stray
        items.net_apply_spawn( rec, /*mirror*/ false );
        items.net_apply_chunk_role( chunk, NET_ROLE_OWNER, empty_records, {} );
        report( "grant: stray spawn still wiped for re-mint", items.debug_items( ).empty( ) );
    }
    return failures;
}

int test_net_mobs( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const glm::dvec3 player_pos( 1.5, 50.0, 1.5 ); // far away: no look-at/sounds

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    for ( int x = 5; x <= 30; x++ ) {
        for ( int z = 5; z <= 15; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }

    // Mirror: no physics — a mob spawned mid-air stays at its net target
    // instead of falling, and only ENTITY_REMOVE takes it away.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        MobManager mobs;
        mobs.set_multiplayer_mode( true, &net );
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 9;
        rec.kind = NetEntityKind::MOB;
        rec.x = 10.5;
        rec.y = 40.0;
        rec.z = 10.5;
        rec.type = MOB_PIG;
        rec.health = 10.0f;
        mobs.net_apply_spawn( rec, /*mirror*/ true );
        check_bool( "mirror: spawned from record", mobs.mobs.size( ) == 1 );
        check_bool( "mirror: flagged mirror", mobs.mobs[ 0 ].net.mirror );
        for ( int i = 0; i < 100; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "mirror: no physics (floats at target)", fabs( mobs.mobs[ 0 ].pos.y - 40.0 ) < 0.01 && !mobs.mobs.empty( ) );

        // DYING rising edge starts the tip-over; nothing removes it locally.
        NetEntityStateEntry e{};
        e.id = rec.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
        e.present = NET_SE_FLAGS;
        e.flags = NET_MOB_FLAG_DYING;
        mobs.net_apply_state( e, player_pos );
        check_bool( "mirror: DYING starts death anim", mobs.mobs[ 0 ].death_ticks > 0 );
        for ( int i = 0; i < 200; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "mirror: still present without ENTITY_REMOVE", mobs.mobs.size( ) == 1 );
        mobs.net_apply_remove( rec.id );
        check_bool( "mirror: ENTITY_REMOVE removes", mobs.mobs.empty( ) );
    }

    // Hostile targeting: a nearer remote targetable player beats the local
    // one and the melee hit carries that player id for PLAYER_HIT.
    {
        MobManager mobs;
        check_bool( "spawn zombie", mobs.spawn( MOB_ZOMBIE, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );
        MobTickInput in{};
        in.player_targetable = true;
        in.world_time = DAY_LENGTH_TICKS / 4;
        in.player_eye_height = PLAYER_EYE_HEIGHT;
        in.remote_targets.push_back( { 7, glm::dvec3( 11.0, 12.5, 10.5 ), 1.62f, true } );
        MobTickOutput out;
        mobs.tick( world, player_pos, &in, &out );
        bool remote_hit = false;
        for ( const MobPlayerHit &hit : out.player_hits ) {
            remote_hit |= hit.target_player_id == 7;
        }
        check_bool( "hostile: melee hit targets remote player", remote_hit );
    }

    // ENTITY_ACTION HIT on an owned mob applies real damage.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        MobManager mobs;
        mobs.set_multiplayer_mode( true, &net );
        check_bool( "action: spawn pig", mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );
        MobManager::Mob &mob = mobs.mobs[ 0 ];
        mob.net.id = 42;
        mob.net.chunk = glm::ivec3( 0, 0, 0 );
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER );
        NetEntityActionPayload p{};
        p.id = 42;
        p.action = NET_ACTION_HIT;
        p.damage = 4.0f;
        p.attacker_survival = 1;
        mobs.net_apply_action( gs, p );
        check_bool( "action: owned mob takes damage", mobs.mobs[ 0 ].health < 10.0f - 0.01f );
    }

    return failures;
}

int test_net_bodies( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    Multiplayer mp;
    NetEntities net( &mp );
    // Use the world's own bodies: TntExplodeEvent consumes the body through
    // repGameState.world.bodies.take(), so a standalone instance wouldn't
    // observe the rescheduled event.
    BlockBodies &bodies = gs.world.bodies;
    bodies.set_multiplayer_mode( true, &net );

    const BlockState sand = { SAND, 0, 0, SAND, 0 };
    const BlockState tnt_state = { TNT, 0, 0, TNT, 0 };

    // Mirror body: interpolation only — it doesn't fall, doesn't land,
    // ignores impulses, and only ENTITY_REMOVE takes it away.
    {
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 1;
        rec.kind = NetEntityKind::BODY;
        rec.x = 10.0;
        rec.y = 40.0;
        rec.z = 10.0;
        rec.type = BlockBodies::BODY_FALLING;
        rec.block_state = sand;
        bodies.net_apply_spawn( gs.world, rec, /*mirror*/ true, gs.tick_number );
        check_bool( "body mirror: spawned", bodies.debug_bodies( ).size( ) == 1 );
        check_bool( "body mirror: flagged mirror", bodies.debug_bodies( )[ 0 ].net.mirror );
        for ( int i = 0; i < 50; i++ ) {
            bodies.tick( gs );
        }
        check_bool( "body mirror: no physics (floats)", fabs( bodies.debug_bodies( )[ 0 ].min.y - 40.0 ) < 0.05 );
        bodies.impulse( glm::dvec3( 10.5, 40.5, 10.5 ), 6.0, 0.05 );
        check_bool( "body mirror: impulse ignored", glm::length( bodies.debug_bodies( )[ 0 ].vel ) < 1e-6 );
        bodies.net_apply_remove( rec.id, NET_REMOVE_DESPAWN );
        check_bool( "body mirror: ENTITY_REMOVE removes", bodies.debug_bodies( ).empty( ) );
    }

    // Owned body: streams ENTITY_STATE without any chunk role (the causer
    // simulates, chunk ownership is irrelevant).
    {
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 2;
        rec.kind = NetEntityKind::BODY;
        rec.x = 10.0;
        rec.y = 40.0;
        rec.z = 10.0;
        rec.type = BlockBodies::BODY_FALLING;
        rec.block_state = sand;
        bodies.net_apply_spawn( gs.world, rec, /*mirror*/ false, gs.tick_number );
        std::vector<NetEntityStateEntry> out;
        bodies.collect_net_state( out, 0 );
        check_bool( "body owned: emits state entry", out.size( ) == 1 && out[ 0 ].id == rec.id );
        bodies.net_apply_remove( rec.id, NET_REMOVE_DESPAWN );
    }

    // ENTITY_TRANSFER adopt=1 for a primed TNT body: becomes owned and the
    // explosion is rescheduled from the wire fuse — it fires at
    // tick_number + remaining and consumes the body.
    {
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 3;
        rec.kind = NetEntityKind::BODY;
        rec.x = 10.0;
        rec.y = 40.0;
        rec.z = 10.0;
        rec.type = BlockBodies::BODY_PRIMED_TNT;
        rec.block_state = tnt_state;
        rec.fuse_ticks = 10;
        bodies.net_apply_transfer( gs, rec );
        check_bool( "body transfer: spawned owned", bodies.debug_bodies( ).size( ) == 1 && !bodies.debug_bodies( )[ 0 ].net.mirror );
        test_run_game_ticks( gs, 12 );
        check_bool( "body transfer: reschedule explodes + consumes body", bodies.debug_bodies( ).empty( ) );
    }

    // BODY_PISTON mirror: the record's pos/vel/fuse_ticks encode the slide —
    // 14 physics ticks from (10,40,10) to (10,40,11). The mirror replays the
    // same kinematic lerp on its own clock.
    {
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 4;
        rec.kind = NetEntityKind::BODY;
        rec.x = 10.0;
        rec.y = 40.0;
        rec.z = 10.0;
        rec.vz = 1.0f / 14.0f; // blocks/physics-tick
        rec.type = BlockBodies::BODY_PISTON;
        rec.block_state = { GOLD_BLOCK, 0, 0, GOLD_BLOCK, 0 };
        rec.fuse_ticks = 14;
        bodies.net_apply_spawn( gs.world, rec, /*mirror*/ true, gs.tick_number );
        check_bool( "piston mirror: spawned", bodies.debug_bodies( ).size( ) == 1 );
        const BlockBodies::Body pb = bodies.debug_bodies( )[ 0 ];
        check_bool( "piston mirror: flagged mirror", pb.net.mirror && pb.kind == BlockBodies::BODY_PISTON );
        check_bool( "piston mirror: from/to rebuilt", glm::length( pb.from - glm::dvec3( 10, 40, 10 ) ) < 0.01 &&
                        glm::length( pb.to - glm::dvec3( 10, 40, 11 ) ) < 0.01 );
        for ( int i = 0; i < 7; i++ ) {
            gs.tick_number++;
            bodies.tick( gs );
        }
        const BlockBodies::Body pm = bodies.debug_bodies( )[ 0 ];
        check_bool( "piston mirror: kinematic mid-slide", pm.min.z > 10.3 && pm.min.z < 10.7 );
        check_bool( "piston mirror: pending destination cell", bodies.piston_move_pending( glm::ivec3( 10, 40, 11 ) ) );
        // Past slide end: holds at the destination until the remove lands
        // it (safety timeout is 2 s later — not hit here).
        for ( int i = 0; i < 20; i++ ) {
            gs.tick_number++;
            bodies.tick( gs );
        }
        check_bool( "piston mirror: holds at slide end", bodies.debug_bodies( ).size( ) == 1 &&
                        glm::length( bodies.debug_bodies( )[ 0 ].min - glm::dvec3( 10, 40, 11 ) ) < 0.01 );
        bodies.net_apply_remove( rec.id, NET_REMOVE_LANDED );
        check_bool( "piston mirror: LANDED remove consumes body", bodies.debug_bodies( ).empty( ) );
        check_bool( "piston mirror: no longer pending", !bodies.piston_move_pending( glm::ivec3( 10, 40, 11 ) ) );
    }

    // ENTITY_TRANSFER for a piston mover (previous sim disconnected): the
    // adopted mover reaches slide end with no piston_moves entry and lands
    // its carried state into the still-MOVING_PISTON destination cell.
    {
        gs.world.set_loaded_block( glm::ivec3( 12, 40, 10 ), { MOVING_PISTON, 0, 0, STONE, 0 } );
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 5;
        rec.kind = NetEntityKind::BODY;
        rec.x = 12.0;
        rec.y = 40.0;
        rec.z = 9.0;
        rec.vz = 1.0f / 14.0f;
        rec.type = BlockBodies::BODY_PISTON;
        rec.block_state = { GOLD_BLOCK, 0, 0, GOLD_BLOCK, 0 };
        rec.fuse_ticks = 14;
        bodies.net_apply_transfer( gs, rec );
        check_bool( "piston transfer: adopted owned", bodies.debug_bodies( ).size( ) == 1 && !bodies.debug_bodies( )[ 0 ].net.mirror );
        for ( int i = 0; i < 30; i++ ) {
            gs.tick_number++;
            bodies.tick( gs );
        }
        const BlockState landed_cell = gs.world.get_loaded_block( glm::ivec3( 12, 40, 10 ) );
        check_bool( "piston transfer: carried state landed", landed_cell.id == GOLD_BLOCK );
        check_bool( "piston transfer: body consumed", bodies.debug_bodies( ).empty( ) );
    }

    return failures;
}

int test_net_item_mirror_visuals( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 200.0, 60.0, 200.0 ); // far away: no magnet/pickup
    test_setup_world( gs.world );
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    gs.world.set_loaded_block( glm::ivec3( 10, 9, 10 ), stone ); // floor under the item

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    Multiplayer mp;
    NetEntities net( &mp );
    ItemEntities items;
    items.set_multiplayer_mode( true, &net );

    NetEntityRecord rec{};
    rec.id = ( static_cast<NetEntityId>( 5 ) << 32 ) | 7;
    rec.kind = NetEntityKind::ITEM;
    rec.x = 10.5;
    rec.y = 10.0; // bottom corner rests on the stone top (y=9 block)
    rec.z = 10.5;
    rec.type = DIRT;
    rec.count = 3;
    items.net_apply_spawn( rec, /*mirror*/ true );
    check_bool( "mirror item: spawned remote", items.test_items( ).size( ) == 1 && items.test_items( )[ 0 ].net.mirror );

    const int age0 = items.test_items( )[ 0 ].age_ticks;
    for ( int i = 0; i < 20; i++ ) {
        gs.tick_number++;
        items.tick( gs );
    }
    ItemEntities::ItemEntity &it = items.test_items( )[ 0 ];
    check_bool( "mirror item: age_ticks is the render clock", it.age_ticks == age0 + 20 );
    check_bool( "mirror item: grounded over solid footing", it.grounded == 1 && it.in_water == 0 );
    check_bool( "mirror item: net-seeded spin matches owner's", fabsf( it.spin_seed - ( ( rec.id * 2654435761ull ) % 1024 ) / 1024.0f * 6.2831853f ) < 1e-5f );
    {
        float yaw, bob;
        ItemEntities::render_pose( it, 0.0f, &yaw, &bob );
        check_bool( "mirror item: bob nonzero while grounded", bob > 0.0f );
    }

    // Mirrors never despawn locally — ENTITY_REMOVE ends them.
    it.age_ticks = ItemEntities::ITEM_DESPAWN_TICKS + 10;
    gs.tick_number++;
    items.tick( gs );
    check_bool( "mirror item: never despawns locally", items.test_items( ).size( ) == 1 );

    return failures;
}

#if defined( __unix__ ) || defined( __APPLE__ )
#include <unistd.h>
#endif

// multiplayer_mode() gates local entity-file persistence. It must report
// multiplayer only while the session is (or was) actually connected: a
// hostname whose connect fails outright leaves a purely local session, and
// gating on the hostname alone silently loses every drop/mob at logout.
// The ever_connected latch keeps a connected-then-dropped session
// multiplayer so server entities still can't mix into local files.
int test_net_multiplayer_mode( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    { // No hostname: never multiplayer.
        Multiplayer mp;
        report( "mode: no hostname is local", !mp.multiplayer_mode( ) );
    }

    { // connect_pending holds the gate while resolution is in flight.
        Multiplayer mp;
        mp.hostname = "example.test";
        mp.connect_pending = true;
        report( "mode: pending connect is mp", mp.multiplayer_mode( ) );
        mp.connect_pending = false;
        report( "mode: failed connect is local", !mp.multiplayer_mode( ) );
    }

    { // Connected then dropped: the latch keeps the session multiplayer.
        Multiplayer mp;
        mp.hostname = "example.test";
        mp.active = true;
        mp.ever_connected = true;
        report( "mode: connected is mp", mp.multiplayer_mode( ) );
        mp.disconnect( );
        report( "mode: dropped stays mp", mp.multiplayer_mode( ) && !mp.is_active( ) );
    }

#if defined( __unix__ ) || defined( __APPLE__ )
    { // Real refused connect resolves to a local session.
        Multiplayer mp;
        mp.init( "127.0.0.1", 1 ); // port 1: nothing listens.
        for ( int i = 0; i < 10000 && mp.is_connecting( ); i++ ) {
            usleep( 1000 );
        }
        report( "mode: refused connect resolves", !mp.is_connecting( ) );
        report( "mode: refused connect is local", !mp.multiplayer_mode( ) && !mp.is_active( ) );
        mp.cleanup( );
    }
#endif
    return failures;
}
