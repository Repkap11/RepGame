#include "common/block_instance_batch.hpp"

#include "common/render_order.hpp"
#include "common/renderer/renderer.hpp"

bool BlockInstanceBatch::Key::operator<( const Key &other ) const {
    if ( order != other.order ) {
        return order < other.order;
    }
    if ( chunk_pos.x != other.chunk_pos.x ) {
        return chunk_pos.x < other.chunk_pos.x;
    }
    if ( chunk_pos.y != other.chunk_pos.y ) {
        return chunk_pos.y < other.chunk_pos.y;
    }
    return chunk_pos.z < other.chunk_pos.z;
}

void BlockInstanceBatch::init( const VertexBuffer &vb_solid_, const VertexBuffer &vb_water_,
                               const VertexBufferLayout &vbl_block_, const VertexBufferLayout &vbl_coords_ ) {
    this->vb_solid = &vb_solid_;
    this->vb_water = &vb_water_;
    this->vbl_block = &vbl_block_;
    this->vbl_coords = &vbl_coords_;
}

void BlockInstanceBatch::destroy( ) {
    for ( auto &entry : this->groups ) {
        Group &g = entry.second;
        if ( g.gl_ready ) {
            g.va.destroy( );
            g.vb.destroy( );
        }
    }
    this->groups.clear( );
}

void BlockInstanceBatch::begin( ) {
    for ( auto it = this->groups.begin( ); it != this->groups.end( ); ) {
        Group &g = it->second;
        if ( !g.touched && g.cpu.empty( ) ) {
            // Stale group (its body left the chunk): free GL and drop it.
            if ( g.gl_ready ) {
                g.va.destroy( );
                g.vb.destroy( );
            }
            it = this->groups.erase( it );
            continue;
        }
        g.cpu.clear( );
        g.touched = false;
        ++it;
    }
}

static BlockCoords *batch_alloc( void *ctx ) {
    std::vector<BlockCoords> &v = *static_cast<std::vector<BlockCoords> *>( ctx );
    v.emplace_back( );
    return &v.back( );
}

BlockEmitSink BlockInstanceBatch::sink( const RenderOrder order, const glm::ivec3 &light_chunk, const int reserve ) {
    Group &g = this->groups[ { static_cast<int>( order ), light_chunk } ];
    g.touched = true;
    // block_emit_cell keeps its base-instance pointer across later allocs,
    // so the vector must never reallocate mid-emit.
    g.cpu.reserve( g.cpu.size( ) + static_cast<size_t>( reserve ) );
    g.water_layout = order == RenderOrder_Water;
    return { &g.cpu, batch_alloc };
}

void BlockInstanceBatch::ensure_gl( Group &g, bool water ) {
    if ( !g.gl_ready ) {
        g.va.init( );
        g.va.add_buffer( *( water ? this->vb_water : this->vb_solid ), *this->vbl_block );
        g.vb.init( );
        g.va.add_buffer( g.vb, *this->vbl_coords );
        g.gl_ready = true;
    }
    g.vb.set_data( g.cpu.data( ), sizeof( BlockCoords ) * g.cpu.size( ) );
}
