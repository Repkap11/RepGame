#pragma once

#include <new>
#include <queue>
#include <utility>
#include <glm/glm.hpp>
#include "common/block_definitions.hpp" // events' signatures use BlockState/BlockID
class BlockUpdateEvent;
struct RepGameState;
class World;

class BlockUpdateOrderCompare {
  public:
    bool operator( )( const BlockUpdateEvent *a, const BlockUpdateEvent *b ) const;
};

class BlockUpdateQueue {
  public:
    BlockUpdateQueue( );
    BlockUpdateQueue( const BlockUpdateQueue & ) = delete;
    BlockUpdateQueue &operator=( const BlockUpdateQueue & ) = delete;
    ~BlockUpdateQueue( );

    // Placement-news an E (a BlockUpdateEvent subclass) into a pooled slot
    // and queues it. The queue owns the event for its whole life — events
    // never outlive it — so slots come from a block pool (seed_fifo_detail's
    // shared 4KB blocks) instead of a heap alloc + shared_ptr per update.
    template <typename E, typename... Args>
    E *addBlockUpdate( Args &&...args ) {
        static_assert( sizeof( E ) <= EVENT_SLOT_BYTES, "BlockUpdateEvent subclass exceeds the event pool slot" );
        E *event = new ( this->slot_alloc( ) ) E( std::forward<Args>( args )... );
        this->pending_events.push( event );
        return event;
    }

    void processAllBlockUpdates( RepGameState &repGameState, long tick_number );
    // The redstone drain: runs every event with tick <= now as one batch.
    void processUpTo( RepGameState &repGameState, long now );

  private:
    static constexpr size_t EVENT_SLOT_BYTES = 128;
    void *slot_alloc( );
    void slot_free( void *p );

    long current_tick;
    std::priority_queue<BlockUpdateEvent *, std::vector<BlockUpdateEvent *>, BlockUpdateOrderCompare> pending_events;
    void *free_slots = nullptr;    // intrusive list through dead slots
    char *block_cursor = nullptr;  // next unallocated slot in the newest block
    char *block_end = nullptr;
    std::vector<void *> pool_blocks; // owned 4KB blocks, returned to the pool at exit
};

class BlockUpdateEvent {
  public:
    BlockUpdateEvent( long tick_number );
    const char *name = "Unnamed BlockUpdateEvent";
    virtual void performAction( RepGameState &repGameState ) = 0;

    long tick_number;
    virtual ~BlockUpdateEvent( ) {
    }
};
