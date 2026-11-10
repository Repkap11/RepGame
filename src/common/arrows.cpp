#include "common/arrows.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include "common/fluid.hpp"
#include "common/mobs.hpp"
#include "common/net_entities.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/world.hpp"

// Per-tick constants at UPS_RATE (144). MC's arrow ballistics at 20 TPS
// (gravity 0.05/tick², drag 0.99, submerged drag 0.6) rescaled to 144:
// ~0.001/tick² gravity, 0.9986 air drag, 0.93 water drag — underwater arrows
// still sink, just crawl.
#define ARROW_GRAVITY 0.001
#define ARROW_AIR_DRAG 0.9986
#define ARROW_WATER_DRAG 0.93
// Collision box around the arrow center: small enough that the rendered
// tip (ARROW_LEN/2 ahead) reads as embedded when the box face stops flush.
#define ARROW_HALF 0.05
// Rendered shaft length: the sprite's extruded 16x16 icon scaled to this.
#define ARROW_LEN 0.7
// Stuck-arrow pickup: overlap of the shaft box against the player's feet
// AABB expanded this much horizontally/vertically (item-pickup feel).
#define ARROW_PICKUP_EXPAND_XZ 0.6
#define ARROW_PICKUP_EXPAND_Y 0.4
static glm::dvec3 arrow_min( const ArrowEntities::Arrow &a ) {
    return a.pos - glm::dvec3( ARROW_HALF );
}

// Flag application shared by the net paths. The anchor cell is never
// streamed: a stuck arrow's vel carries the entry direction (kept for
// mirror orientation), so the tip cell falls out of pos + dir and can be
// re-derived wherever the parked state lands — including adoption after
// the sim disconnects.
static void arrow_apply_net_flags( ArrowEntities::Arrow &a, const uint32_t flags, const glm::dvec3 &pos, const glm::dvec3 &vel ) {
    a.stuck = ( flags & NET_ARROW_FLAG_STUCK ) ? 1 : 0;
    a.attacker_survival = ( flags & NET_ARROW_FLAG_ATTACKER_SURVIVAL ) ? 1 : 0;
    if ( a.stuck ) {
        const glm::dvec3 dir = glm::length( vel ) > 1e-6 ? glm::normalize( vel ) : a.orient;
        a.stuck_cell = glm::ivec3( glm::floor( pos + dir * ( ARROW_LEN * 0.5 ) ) );
    }
}

bool ArrowEntities::arrow_chunk_live( World &world, const Arrow &a ) const {
    const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( glm::floor( a.pos ) ) );
    return chunk != nullptr && chunk->load_state == CHUNK_LIVE;
}

void ArrowEntities::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_item_instance ) {
    int nv = 0, ni = 0;
    const ParticleVertex *vb = item_sprite_mesh_vertices( &nv );
    const unsigned int *ib = item_sprite_mesh_indices( &ni );
    this->render_chain.init( vbl_object_vertex, vbl_item_instance, vb, nv, ib, ni );
    this->render_inited = true;
}

uint32_t ArrowEntities::spawn( const glm::dvec3 &pos, const glm::dvec3 &vel, const float damage, const int attacker_survival, const bool from_net ) {
    if ( this->arrows.size( ) >= MAX_ARROWS ) {
        // Evict the oldest arrow (lowest id).
        size_t oldest = 0;
        for ( size_t i = 1; i < this->arrows.size( ); i++ ) {
            if ( this->arrows[ i ].id < this->arrows[ oldest ].id ) {
                oldest = i;
            }
        }
        remove_arrow( oldest );
    }
    Arrow a;
    a.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    a.pos = pos;
    a.prev_pos = pos;
    a.vel = vel;
    a.orient = glm::length( vel ) > 1e-9 ? glm::normalize( vel ) : glm::dvec3( 0.0, 1.0, 0.0 );
    a.damage = damage;
    a.attacker_survival = attacker_survival;
    this->arrows.push_back( a );
    Arrow &back = this->arrows.back( );
    back.net.chunk = net_entity_chunk_of( pos );
    if ( this->net != nullptr && !from_net ) {
        this->announce_spawn( back );
    }
    return a.id;
}

void ArrowEntities::remove_arrow( const size_t i ) {
    Arrow &a = this->arrows[ i ];
    if ( a.entity != entt::null ) {
        this->render_chain.remove( a.entity );
        a.entity = entt::null;
    }
    this->arrows.erase( this->arrows.begin( ) + i );
}

void ArrowEntities::tick( RepGameState &state, ArrowTickOutput *out ) {
    World &world = state.world;
    // Bind the sync hub while multiplayer is on; entities with net.id 0
    // behave exactly like single player.
    this->net = this->mp_mode ? &state.multiplayer.entities : nullptr;
    const bool freeze = RepGame::capture_mode( );

    // Player feet AABB (camera pos is the eye position) for stuck-arrow pickup.
    const double feet_y = state.camera.pos.y - state.camera.eye_height;
    const double half_w = PLAYER_WIDTH * 0.5;

    static const std::vector<BodyBox> no_extra;
    std::vector<size_t> dead;
    for ( size_t i = 0; i < this->arrows.size( ); i++ ) {
        Arrow &a = this->arrows[ i ];
        a.prev_pos = a.pos;
        if ( freeze || !arrow_chunk_live( world, a ) ) {
            continue;
        }

        if ( a.net.mirror ) {
            // Mirror of a remote-simulated arrow: no physics — ease toward
            // the last streamed pose. vel tracks net.vel so the render
            // orientation follows the stream (stuck arrows keep their
            // entry direction).
            const glm::dvec3 delta = a.net.pos - a.pos;
            if ( glm::length( delta ) > 4.0 ) {
                a.pos = a.net.pos;
            } else {
                a.pos += delta * 0.35;
            }
            a.vel = a.net.vel;
            if ( glm::length( a.vel ) > 1e-6 ) {
                a.orient = glm::normalize( a.vel );
            }
            continue;
        }

        a.age_ticks++;

        if ( a.stuck ) {
            // The block holding the tip broke: drop free from rest, keeping
            // the entry orientation until gravity bends it.
            const BlockState anchor = world.get_loaded_block( a.stuck_cell );
            if ( anchor.id == LAST_BLOCK_ID || !block_definition_get_definition( anchor.id )->collides_with_player ) {
                a.stuck = 0;
                a.vel = glm::dvec3( 0.0 );
            } else {
                // Walk-over pickup: shaft box vs feet AABB (expanded a
                // little). A full inventory leaves the arrow in place.
                const glm::dvec3 amin = a.pos - glm::dvec3( ARROW_LEN * 0.5 );
                const glm::dvec3 amax = a.pos + glm::dvec3( ARROW_LEN * 0.5 );
                if ( amax.x > state.camera.pos.x - half_w - ARROW_PICKUP_EXPAND_XZ && amin.x < state.camera.pos.x + half_w + ARROW_PICKUP_EXPAND_XZ && //
                     amax.z > state.camera.pos.z - half_w - ARROW_PICKUP_EXPAND_XZ && amin.z < state.camera.pos.z + half_w + ARROW_PICKUP_EXPAND_XZ && //
                     amax.y > feet_y - ARROW_PICKUP_EXPAND_Y && amin.y < feet_y + state.camera.player_height + ARROW_PICKUP_EXPAND_Y ) {
                    const int leftover = RepGame::give_item_to_player( state, ARROW, 1 );
                    if ( leftover == 0 ) {
                        audio_play_sfx_gain( "pop", 0.5f );
                        // The pickup may have filled the selected hotbar
                        // slot — refresh the held-item overlay.
                        state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
                        this->send_net_remove( a, NET_REMOVE_PICKED_UP );
                        dead.push_back( i );
                        continue;
                    }
                }
                if ( a.age_ticks >= ARROW_DESPAWN_TICKS ) {
                    this->send_net_remove( a, NET_REMOVE_DESPAWN );
                    dead.push_back( i );
                }
                continue;
            }
        }

        // Medium: lava and open flame destroy the arrow on contact (the
        // fizz feedback of dropped items).
        const glm::ivec3 medium_cell( glm::floor( a.pos ) );
        const BlockState medium = world.get_loaded_block( medium_cell );
        const BlockID medium_fluid = fluid_in_cell( medium );
        if ( medium_fluid == LAVA || medium.id == FIRE || medium.id == SOUL_FIRE ) {
            const float burn_dist = static_cast<float>( glm::length( a.pos - state.camera.pos ) );
            if ( burn_dist < 16.0f ) {
                audio_play_sfx_gain( "fizz", 1.0f - burn_dist / 16.0f );
            }
            this->send_net_remove( a, NET_REMOVE_DIED );
            dead.push_back( i );
            continue;
        }

        // Integrate: gravity, then medium drag.
        a.vel.y -= ARROW_GRAVITY;
        a.vel *= medium_fluid != LAST_BLOCK_ID ? ARROW_WATER_DRAG : ARROW_AIR_DRAG;
        if ( glm::length( a.vel ) > 1e-6 ) {
            a.orient = glm::normalize( a.vel );
        }

        const double seg_len = glm::length( a.vel );
        if ( seg_len < 1e-9 ) {
            continue;
        }
        const glm::dvec3 dir = a.vel / seg_len;

        // First mob hitbox along the full intended segment — resolved
        // against the block hit below so a nearer wall wins.
        double mob_t = 0.0;
        const int mob = world.mobs.raycast( a.pos, dir, seg_len, &mob_t );

        // Move a tiny box at the arrow center: stops flush on the first
        // colliding face (plants/torches don't collide), so the tip reads
        // as embedded once the pose is adjusted below.
        glm::dvec3 amin = arrow_min( a );
        glm::dvec3 amax = amin + glm::dvec3( ARROW_HALF * 2 );
        const glm::dvec3 want = a.vel;
        bool hit_axis[ 3 ];
        int hit_extra_axis[ 3 ];
        bool standing = false;
        Collision::move_box( world, amin, amax, want, no_extra, hit_axis, hit_extra_axis, &standing );
        const glm::dvec3 landed = amin + glm::dvec3( ARROW_HALF );
        const double traveled = glm::length( landed - a.pos );

        if ( mob >= 0 && mob_t <= traveled + 1e-4 ) {
            // The mob's box entry is nearer than whatever stopped the box.
            a.pos = a.pos + dir * mob_t;
            if ( out != nullptr ) {
                out->mob_hits.push_back( { mob, a.pos, a.damage, a.attacker_survival } );
            }
            audio_play_sfx( "arrow_hit" );
            this->send_net_remove( a, NET_REMOVE_DIED );
            dead.push_back( i );
            continue;
        }
        a.pos = landed;
        if ( hit_axis[ 0 ] || hit_axis[ 1 ] || hit_axis[ 2 ] ) {
            // Stick: seat the center so the tip embeds ~0.1 into the face
            // (landed sits ARROW_HALF back from it). vel keeps the entry
            // direction — it streams to mirrors as the stuck orientation.
            a.pos += dir * ( ARROW_HALF + 0.1 - ARROW_LEN * 0.5 );
            a.stuck = 1;
            a.stuck_cell = glm::ivec3( glm::floor( a.pos + dir * ( ARROW_LEN * 0.5 ) ) );
            audio_play_sfx( "arrow_hit" );
        }
    }

    std::sort( dead.rbegin( ), dead.rend( ) );
    for ( const size_t i : dead ) {
        remove_arrow( i );
    }
}

void ArrowEntities::update_render( const float alpha, const ChunkLoader &chunk_loader ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( Arrow &a : this->arrows ) {
        const glm::dvec3 rpos = glm::mix( a.prev_pos, a.pos, static_cast<double>( alpha ) );
        if ( a.entity == entt::null ) {
            const std::pair<entt::entity, ItemSpriteInstance &> data = this->render_chain.create_instance( );
            a.entity = data.first;
            data.second.tex_layer = item_icon_texture( ARROW );
        }
        ItemSpriteInstance &inst = this->render_chain.get_instance( a.entity );
        // Velocity-aligned shaft: the sprite's +y is the flight direction
        // (the icon's diagonal is rotated in-plane onto it). The sprite
        // plane is the vertical plane through the shaft (+x is world-up),
        // so the arrow keeps a fixed world orientation instead of rolling
        // toward the camera; viewed shaft-on it thins to its extruded
        // edge like a real arrow. Vertical flight degenerates the up
        // reference, so swap in +x for that.
        const glm::vec3 fwd = glm::length( a.orient ) > 1e-6 ? glm::vec3( glm::normalize( a.orient ) ) : glm::vec3( 0.0f, 1.0f, 0.0f );
        const glm::vec3 up = fabsf( fwd.y ) > 0.999f ? glm::vec3( 1.0f, 0.0f, 0.0f ) : glm::vec3( 0.0f, 1.0f, 0.0f );
        const glm::vec3 normal = glm::normalize( glm::cross( up, fwd ) );
        const float s = static_cast<float>( ARROW_LEN );
        glm::mat4 m( 0.0f );
        m[ 0 ] = glm::vec4( up * s, 0.0f );
        m[ 1 ] = glm::vec4( fwd * s, 0.0f );
        m[ 2 ] = glm::vec4( normal * s, 0.0f );
        m[ 3 ] = glm::vec4( glm::vec3( rpos ), 1.0f );
        // Recentre the unit-square sprite on the shaft, then rotate the
        // icon's diagonal onto +y with the tip leading (the mesh's +y
        // samples image-down, so the tail→tip diagonal maps to +y at
        // 315°, not 135°).
        m = m * glm::translate( glm::mat4( 1.0f ), glm::vec3( 0.5f, 0.5f, 0.5f / 16.0f ) );
        m = m * glm::rotate( glm::mat4( 1.0f ), glm::radians( 315.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
        m = m * glm::translate( glm::mat4( 1.0f ), glm::vec3( -0.5f, -0.5f, -0.5f / 16.0f ) );
        inst.transform = m;
        inst.light = chunk_loader.light_sample( glm::vec3( rpos ) );
    }
    this->render_chain.invalidate_all( );
}

void ArrowEntities::draw( const Renderer &renderer, const Shader &item_sprite_shader ) {
    if ( !this->render_inited ) {
        return;
    }
    this->render_chain.draw( renderer, item_sprite_shader );
}

void ArrowEntities::cleanup( ) {
    this->render_chain.clear( );
    this->arrows.clear( );
}

// ---------------------------------------------------------------------------
// Multiplayer sync ("causer simulates", like BlockBodies): the spawning
// client runs the flight sim and streams ENTITY_STATE; everyone else holds
// mirrors. Role drops never move arrow ownership — the record rides in the
// entity's current chunk.
// ---------------------------------------------------------------------------

void ArrowEntities::set_multiplayer_mode( const bool mp_mode, NetEntities *net ) {
    this->mp_mode = mp_mode;
    this->net = mp_mode ? net : nullptr;
}

void ArrowEntities::announce_spawn( Arrow &a ) {
    if ( this->net == nullptr || a.net.id != 0 ) {
        return;
    }
    const NetEntityId nid = this->net->mint_id( );
    if ( nid == 0 ) {
        return;
    }
    a.net.id = nid;
    NetEntityRecord rec;
    rec.id = nid;
    rec.kind = NetEntityKind::ARROW;
    rec.x = a.pos.x;
    rec.y = a.pos.y;
    rec.z = a.pos.z;
    rec.vx = static_cast<float>( a.vel.x );
    rec.vy = static_cast<float>( a.vel.y );
    rec.vz = static_cast<float>( a.vel.z );
    rec.health = a.damage;
    rec.age_ticks = a.age_ticks;
    rec.flags = ( a.stuck ? NET_ARROW_FLAG_STUCK : 0u ) | ( a.attacker_survival ? NET_ARROW_FLAG_ATTACKER_SURVIVAL : 0u );
    this->net->send_spawn( rec );
}

void ArrowEntities::send_net_remove( Arrow &a, const uint8_t reason ) {
    netsync_send_remove( this->net, a.net, reason );
}

ArrowEntities::Arrow *ArrowEntities::find_by_net_id( const NetEntityId net_id ) {
    return netsync_find( this->arrows, net_id );
}

bool ArrowEntities::spawn_record( const NetEntityRecord &rec, const bool mirror ) {
    if ( this->arrows.size( ) >= MAX_ARROWS ) {
        return false;
    }
    Arrow a;
    a.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    a.pos = glm::dvec3( rec.x, rec.y, rec.z );
    a.prev_pos = a.pos;
    a.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
    a.orient = glm::length( a.vel ) > 1e-6 ? glm::normalize( a.vel ) : glm::dvec3( 0.0, 1.0, 0.0 );
    a.damage = rec.health > 0.0f ? rec.health : ARROW_DAMAGE;
    arrow_apply_net_flags( a, rec.flags, a.pos, a.vel );
    a.age_ticks = rec.age_ticks;
    netsync_apply_record( a.net, rec, mirror );
    this->arrows.push_back( a );
    return true;
}

void ArrowEntities::net_apply_spawn( const NetEntityRecord &rec, const bool mirror ) {
    Arrow *existing = this->find_by_net_id( rec.id );
    if ( existing != nullptr ) {
        // Already local (e.g. our own echo or a relayed re-spawn): retarget
        // mirrors; owned arrows keep simulating.
        if ( existing->net.mirror ) {
            existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
            existing->net.chunk = net_entity_chunk_of( existing->net.pos );
            arrow_apply_net_flags( *existing, rec.flags, existing->net.pos, existing->net.vel );
        }
        return;
    }
    this->spawn_record( rec, mirror );
}

void ArrowEntities::net_apply_state( const NetEntityStateEntry &e ) {
    Arrow *a = this->find_by_net_id( e.id );
    if ( a == nullptr || !a->net.mirror ) {
        return;
    }
    a->net.pos = glm::dvec3( e.x, e.y, e.z );
    a->net.vel = glm::dvec3( e.vx, e.vy, e.vz );
    arrow_apply_net_flags( *a, e.flags, a->net.pos, a->net.vel );
    netsync_track_chunk( a->net, a->net.pos );
}

void ArrowEntities::net_apply_remove( const NetEntityId net_id ) {
    for ( size_t i = 0; i < this->arrows.size( ); i++ ) {
        if ( this->arrows[ i ].net.id == net_id && net_id != 0 ) {
            remove_arrow( i );
            return;
        }
    }
}

void ArrowEntities::net_apply_transfer( const NetEntityRecord &rec ) {
    // Sim handover after the causer disconnected: we now own the arrow.
    Arrow *a = this->find_by_net_id( rec.id );
    if ( a == nullptr ) {
        if ( !this->spawn_record( rec, false ) ) {
            return;
        }
        a = &this->arrows.back( );
    } else {
        // Resume the sim from the last streamed pose rather than the
        // mirror's eased approximation.
        a->pos = a->net.pos;
        a->vel = a->net.vel;
    }
    a->net.mirror = false;
    a->net.sent = false; // stream resumes with the next collect
}

void ArrowEntities::net_apply_chunk_role( const glm::ivec3 &chunk, const uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids ) {
    if ( role == NET_ROLE_NONE ) {
        for ( size_t i = 0; i < this->arrows.size( ); ) {
            if ( this->arrows[ i ].net.mirror && this->arrows[ i ].net.chunk == chunk ) {
                remove_arrow( i );
            } else {
                i++;
            }
        }
        return;
    }
    // VIEWER/OWNER: sync mirror arrows to the record list. Roles don't move
    // arrow ownership — arrows we simulate are left alone.
    std::unordered_map<NetEntityId, const NetEntityRecord *> by_id;
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind == NetEntityKind::ARROW ) {
            by_id[ rec.id ] = &rec;
        }
    }
    for ( size_t i = 0; i < this->arrows.size( ); ) {
        const Arrow &a = this->arrows[ i ];
        if ( a.net.mirror && a.net.id != 0 && a.net.chunk == chunk && by_id.find( a.net.id ) == by_id.end( ) && !keep_ids.count( a.net.id ) ) {
            remove_arrow( i );
        } else {
            i++;
        }
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::ARROW ) {
            continue;
        }
        Arrow *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            if ( existing->net.mirror ) {
                existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
                existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
                // The grant's chunk is authoritative (margin records have
                // floor(pos) != the server's chunk).
                existing->net.chunk = chunk;
                arrow_apply_net_flags( *existing, rec.flags, existing->net.pos, existing->net.vel );
            }
        } else if ( this->spawn_record( rec, true ) ) {
            this->arrows.back( ).net.chunk = chunk;
        }
    }
}

void ArrowEntities::net_disconnect( ) {
    // Mirror arrows freeze mid-flight without their sim — drop them. Arrows
    // we simulate keep flying locally.
    for ( size_t i = 0; i < this->arrows.size( ); ) {
        if ( this->arrows[ i ].net.mirror ) {
            remove_arrow( i );
        } else {
            i++;
        }
    }
}

void ArrowEntities::collect_net_state( std::vector<NetEntityStateEntry> &out, const long tick_number ) {
    for ( Arrow &a : this->arrows ) {
        if ( a.net.id == 0 || a.net.mirror ) {
            continue;
        }
        const bool keyframe = netsync_keyframe_due( a.net, tick_number );
        const bool changed = !a.net.sent || netsync_moved( a.net, a.pos, a.vel );
        if ( !changed && !keyframe ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = a.net.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::ARROW );
        e.x = a.pos.x;
        e.y = a.pos.y;
        e.z = a.pos.z;
        e.vx = static_cast<float>( a.vel.x );
        e.vy = static_cast<float>( a.vel.y );
        e.vz = static_cast<float>( a.vel.z );
        e.flags = ( a.stuck ? NET_ARROW_FLAG_STUCK : 0u ) | ( a.attacker_survival ? NET_ARROW_FLAG_ATTACKER_SURVIVAL : 0u );
        out.push_back( e );
        // The server now applies its boundary hysteresis to this pos.
        netsync_mark_sent( a.net, a.pos, a.vel, tick_number, keyframe );
    }
}
