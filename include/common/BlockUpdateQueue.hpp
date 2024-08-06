#ifndef HEADER_BLOCK_UPDATE_QUEUE_H
#define HEADER_BLOCK_UPDATE_QUEUE_H

#include <queue>
#include <memory>
#include "common/BlockUpdateQueue.hpp"
class BlockUpdateEvent;
struct RepGameState;

class BlockUpdateOrderCompare {
  public:
    bool operator( )( std::shared_ptr<BlockUpdateEvent> a, std::shared_ptr<BlockUpdateEvent> b );
};

class BlockUpdateQueue {
  public:
    BlockUpdateQueue( );
    void addBlockUpdate( std::shared_ptr<BlockUpdateEvent> event );
    void processAllBlockUpdates( RepGameState &repGameState, long tick_number );

  private:
    long current_tick;
    std::priority_queue<std::shared_ptr<BlockUpdateEvent>, std::vector<std::shared_ptr<BlockUpdateEvent>>, BlockUpdateOrderCompare> pending_events;
};
#include "common/RepGame.hpp"

class BlockUpdateEvent {
  public:
    BlockUpdateEvent( long tick_number );
    const char *name = "Unnamed BlockUpdateEvent";
    virtual void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) = 0;

    long tick_number;
    virtual ~BlockUpdateEvent( ) {
    }
};

#endif