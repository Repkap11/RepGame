#pragma once

#include <queue>
#include <memory>
#include "common/BlockUpdateQueue.hpp"
class BlockUpdateEvent;
struct RepGameState;

class BlockUpdateOrderCompare {
  public:
    bool operator( )( const std::shared_ptr<BlockUpdateEvent> &a, const std::shared_ptr<BlockUpdateEvent> &b ) const;
};

class BlockUpdateQueue {
  public:
    BlockUpdateQueue( );
    void addBlockUpdate( const std::shared_ptr<BlockUpdateEvent> &event );
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