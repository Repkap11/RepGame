#ifndef HEADER_BLOCK_UPDATE_QUEUE_H
#define HEADER_BLOCK_UPDATE_QUEUE_H

#include <queue>
#include <memory>
#include "common/BlockUpdateQueue.hpp"
#include "common/world.hpp"

class BlockUpdateEvent;

class BlockUpdateOrderCompare {
  public:
    bool operator( )( std::shared_ptr<BlockUpdateEvent> a, std::shared_ptr<BlockUpdateEvent> b );
};

class BlockUpdateQueue {
  public:
    BlockUpdateQueue( );
    void addBlockUpdate( BlockUpdateEvent &event );
    void processAllBlockUpdates( World &world, long tick_number );

  private:
    long current_tick;
    std::priority_queue<std::shared_ptr<BlockUpdateEvent>, std::vector<std::shared_ptr<BlockUpdateEvent>>, BlockUpdateOrderCompare> pending_events;
};

class BlockUpdateEvent {
  public:
    BlockUpdateEvent( long tick_number );
    const char *name = "Unnamed BlockUpdateEvent";
    virtual void performAction( BlockUpdateQueue &blockUpdateQueue, World &world ) = 0;

    long tick_number;
    virtual ~BlockUpdateEvent( ) {
    }
};

#endif