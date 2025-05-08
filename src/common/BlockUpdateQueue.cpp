#include "common/RepGame.hpp"
#include "common/BlockUpdateQueue.hpp"

BlockUpdateQueue::BlockUpdateQueue( ) : current_tick( 0 ) {
}
void BlockUpdateQueue::addBlockUpdate( const std::shared_ptr<BlockUpdateEvent> &event ) {
    this->pending_events.push( event );
}

void BlockUpdateQueue::processAllBlockUpdates( RepGameState &repGameState, const long tick_number ) {
    int num_events = 0;
    this->current_tick = tick_number;
    while ( !this->pending_events.empty( ) ) {
        std::shared_ptr<BlockUpdateEvent> event_prt = this->pending_events.top( );
        if ( event_prt->tick_number >= this->current_tick ) {
            break;
        }
        this->pending_events.pop( );
        event_prt->performAction( *this, repGameState );
        num_events++;
        if ( num_events > 10000 ) {
            pr_debug( "Error too many events!!!" );
            exit( 1 );
        }
    }
}

bool BlockUpdateOrderCompare::operator( )( const std::shared_ptr<BlockUpdateEvent> &a, const std::shared_ptr<BlockUpdateEvent> &b ) const {
    return a->tick_number > b->tick_number;
}

BlockUpdateEvent::BlockUpdateEvent( const long tick_number ) : tick_number( tick_number ) {};
