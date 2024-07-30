#include "common/RepGame.hpp"

struct position {
    int x;
    int y;
};

struct id {
    int id;
};

int test_ecs( ) {
    pr_debug( "test_ecs" );

    // we can create a entt::registry to store our entities
    entt::registry registry;

    // we'll create 10 entities
    for ( std::size_t i = 0; i < 10; ++i ) {
        const auto entity = registry.create( );
        registry.emplace<position>( entity, i, -i );
        if ( i % 2 == 0 ) {
            registry.emplace<id>( entity, i );
        }
    }

    auto view_both = registry.view<const position, const id>( );
    for ( auto [ entity, pos, id ] : view_both.each( ) ) {
        pr_debug( "Id:%d pos:%d %d", id.id, pos.x, pos.y );
    }

    auto group_pos = registry.group<position>( );
    auto size = group_pos.size( );
    auto pos = *group_pos.storage<position>( )->raw( );

    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "pos:%d %d", pos[ i ].x, pos[ i ].y );
    }

    return 0;
}