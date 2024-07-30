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

    entt::entity entity_3 = entt::null;

    for ( int i = 0; i < 10; ++i ) {
        const entt::entity entity = registry.create( );
        registry.emplace<position>( entity, i, -i );
        if ( i == 3 ) {
            entity_3 = entity;
        }
    }

    auto group_pos = registry.group<position>( );
    auto size = group_pos.size( );
    auto pos = *group_pos.storage<position>( )->raw( );

    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "B pos:%d %d", pos[ i ].x, pos[ i ].y );
    }

    if ( entity_3 != entt::null ) {
        registry.destroy( entity_3 );
    }
    size = group_pos.size( );
    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "A pos:%d %d", pos[ i ].x, pos[ i ].y );
    }
    return 0;
}