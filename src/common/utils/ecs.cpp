#include "common/RepGame.hpp"

struct position {
    int x;
    int y;
};

struct Tag {
    int id;
};

int test_ecs( ) {
    pr_debug( "test_ecs" );

    entt::registry registry;
    for ( std::size_t i = 0; i < 10; ++i ) {
        const entt::entity entity = registry.create( );
        registry.emplace<position>( entity, i, -i );
        if ( i == 3 ) {
            registry.emplace<Tag>( entity, i );
        }
    }

    auto group_pos = registry.group<position>( );
    auto size = group_pos.size( );
    auto pos = *group_pos.storage<position>( )->raw( );

    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "B pos:%d %d", pos[ i ].x, pos[ i ].y );
    }

    auto entities_with_id = registry.view<const Tag>( );
    for ( auto [ entity, id ] : entities_with_id.each( ) ) {
        if ( id.id == 3 ) {
            registry.destroy( entity );
        }
    }
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