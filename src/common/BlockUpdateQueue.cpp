#include "common/BlockUpdateQueue.hpp"
#include "common/RepGame.hpp"

BlockUpdateQueue::BlockUpdateQueue( ) {
}
void BlockUpdateQueue::addBlockUpdate( BlockUpdateEvent *event ) {
    std::shared_ptr<BlockUpdateEvent> event_prt;
    event_prt.reset( event );
    this->pending_events.push( event_prt );
}

void BlockUpdateQueue::processAllBlockUpdates( World *world, long tick_number ) {
    int num_events = 0;
    this->current_tick = tick_number;
    while ( !this->pending_events.empty( ) ) {
        std::shared_ptr<BlockUpdateEvent> event_prt = this->pending_events.top( );
        if ( event_prt->tick_number >= this->current_tick ) {
            break;
        }
        this->pending_events.pop( );
        event_prt->performAction( this, world );
        num_events++;
        if ( num_events > 10000 ) {
            pr_debug( "Error too many events!!!" );
            exit( 1 );
            return;
        }
    }
}

bool BlockUpdateOrderCompare::operator( )( std::shared_ptr<BlockUpdateEvent> a, std::shared_ptr<BlockUpdateEvent> b ) {
    return a->tick_number > b->tick_number;
}

BlockUpdateEvent::BlockUpdateEvent( long tick_number ) : tick_number( tick_number ){};
