#include "common/RepGame.hpp"
#include "common/BlockUpdateQueue.hpp"
#include "common/utils/seed_fifo.hpp"

BlockUpdateQueue::BlockUpdateQueue( ) : current_tick( 0 ) {
}

BlockUpdateQueue::~BlockUpdateQueue( ) {
    while ( !this->pending_events.empty( ) ) {
        BlockUpdateEvent *event = this->pending_events.top( );
        this->pending_events.pop( );
        event->~BlockUpdateEvent( );
    }
    for ( void *block : this->pool_blocks ) {
        seed_fifo_detail::block_put( block );
    }
}

void *BlockUpdateQueue::slot_alloc( ) {
    if ( this->free_slots ) {
        void *p = this->free_slots;
        this->free_slots = *static_cast<void **>( p );
        return p;
    }
    if ( this->block_cursor == this->block_end ) {
        char *block = static_cast<char *>( seed_fifo_detail::block_get( ) );
        this->pool_blocks.push_back( block );
        this->block_cursor = block;
        this->block_end = block + seed_fifo_detail::BLOCK_BYTES;
    }
    void *p = this->block_cursor;
    this->block_cursor += EVENT_SLOT_BYTES;
    return p;
}

void BlockUpdateQueue::slot_free( void *p ) {
    *static_cast<void **>( p ) = this->free_slots;
    this->free_slots = p;
}

void BlockUpdateQueue::processAllBlockUpdates( RepGameState &repGameState, const long tick_number ) {
    int num_events = 0;
    this->current_tick = tick_number;
    while ( !this->pending_events.empty( ) ) {
        BlockUpdateEvent *event = this->pending_events.top( );
        if ( event->tick_number >= this->current_tick ) {
            break;
        }
        this->pending_events.pop( );
        event->performAction( *this, repGameState );
        num_events++;
        if ( num_events > 100000 ) {
            pr_debug( "Error too many events!!!" );
            repgame_fatal( );
        }
        event->~BlockUpdateEvent( );
        this->slot_free( event );
    }
}

bool BlockUpdateOrderCompare::operator( )( const BlockUpdateEvent *a, const BlockUpdateEvent *b ) const {
    return a->tick_number > b->tick_number;
}

BlockUpdateEvent::BlockUpdateEvent( const long tick_number ) : tick_number( tick_number ) {};
