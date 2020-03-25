#include "common/BlockUpdateQueue.hpp"
#include "common/RepGame.hpp"

BlockUpdateQueue::BlockUpdateQueue( ) {
}
void BlockUpdateQueue::addBlockUpdate( BlockUpdateEvent *event ) {
    std::shared_ptr<BlockUpdateEvent> event_prt;
    event_prt.reset( event );
    this->pending_events.push( event_prt );
}
void BlockUpdateQueue::processAllBlockUpdates( World *world ) {
    while ( !this->pending_events.empty( ) ) {
        std::shared_ptr<BlockUpdateEvent> event_prt = this->pending_events.front( );
        pr_debug( "Process event %s", event_prt->name );

        this->pending_events.pop( );
        event_prt->performAction(this, world );
    }
}
