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

    const auto entity_3 = registry.create( );
    for ( std::size_t i = 0; i < 10; ++i ) {
        entt::entity entity;
        if ( i == 3 ) {
            entity = entity_3;
        } else {
            entity = registry.create( );
        }
        registry.emplace<position>( entity, i, -i );
        registry.emplace<id>( entity, i );
    }

    // auto view_both = registry.view<const position, const id>( );
    // for ( auto [ entity, pos, id ] : view_both.each( ) ) {
    //     pr_debug( "B Id:%d pos:%d %d", id.id, pos.x, pos.y );
    // }

    auto group_pos = registry.group<position>( );
    auto size = group_pos.size( );
    auto pos = *group_pos.storage<position>( )->raw( );

    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "B pos:%d %d", pos[ i ].x, pos[ i ].y );
    }
    registry.destroy( entity_3 );

    group_pos = registry.group<position>( );
    size = group_pos.size( );
    pos = *group_pos.storage<position>( )->raw( );
    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "A pos:%d %d", pos[ i ].x, pos[ i ].y );
    }

    // for ( auto [ entity, pos, id ] : view_both.each( ) ) {
    //     pr_debug( "A Id:%d pos:%d %d", id.id, pos.x, pos.y );
    // }

    return 0;
}