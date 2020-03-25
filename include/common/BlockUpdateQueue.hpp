#ifndef HEADER_BLOCK_UPDATE_QUEUE_H
#define HEADER_BLOCK_UPDATE_QUEUE_H

#include <queue>
#include <memory>
#include "common/BlockUpdateQueue.hpp"
#include "common/world.hpp"

class BlockUpdateEvent {
  public:
    const char *name = "Unnamed BlockUpdateEvent";
    virtual void performAction( World *world  ) = 0;
    virtual ~BlockUpdateEvent( ) {
    }
};

class BlockUpdateQueue {
  public:
    BlockUpdateQueue( );
    void addBlockUpdate( BlockUpdateEvent *event );
    void processAllBlockUpdates( World *world  );

  private:
    std::queue<std::shared_ptr<BlockUpdateEvent>> pending_events;
};

#endif