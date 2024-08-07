#include "common/RepGame.hpp"
#include <random>

void ECS::print_group( const std::string &msg ) {
    pr_debug( "%s", msg.c_str( ) );

    // auto registry_group = registry.group<Tag, Position>( );
    // auto registry_group = registry.group<Position, Tag>( );
    auto registry_group = registry.group<Position>( entt::get<Tag> );
    // auto registry_group = registry.group<Tag>( entt::get<Position> );
    // auto registry_group = registry.group<>( entt::get<Position, Tag> );

    registry_group.sort<Tag>( []( const Tag &lhs, const Tag &rhs ) { return lhs.id > rhs.id; } );

    Position *pos = *registry_group.storage<Position>( )->raw( );
    Tag *tag = *registry_group.storage<Tag>( )->raw( );
    std::size_t size_both = registry_group.size( );
    std::size_t size_no_tag = registry_group.storage<Position>( )->size( );

    for ( std::size_t i = 0; i < size_both; ++i ) {
        pr_debug( "both tag:%d pos:%d %d", tag[ i ].id, pos[ i ].x, pos[ i ].y );
    }
    for ( std::size_t i = size_both; i < size_no_tag; ++i ) {
        pr_debug( "notag pos:%d %d", pos[ i ].x, pos[ i ].y );
    }
}

void ECS::print_view( const std::string &msg ) {
    pr_debug( "%s", msg.c_str( ) );

    auto registry_view = registry.view<Tag, Position>( );
    registry_view.use<Tag>( );

    Position *pos = *registry_view.storage<Position>( )->raw( );
    Tag *tag = *registry_view.storage<Tag>( )->raw( );
    // std::size_t size = registry_view.storage<Position>( )->size( );
    std::size_t size = registry_view.size_hint( );

    // for ( auto [ entity, tag, pos ] : registry_view.each( ) ) {
    //     pr_debug( "itr tag:%d pos:%d %d", tag.id, pos.x, pos.y );
    // }

    for ( std::size_t i = 0; i < size; ++i ) {
        pr_debug( "ptr tag:%d pos:%d %d", tag[ i ].id, pos[ i ].x, pos[ i ].y );
    }
}

int ECS::test_ecs( ) {
    pr_debug( "test_ecs" );

    entt::entity entity_3 = entt::null;
    entt::entity entity_8 = entt::null;
    for ( int i = 0; i < 20; ++i ) {
        const entt::entity entity = registry.create( );
        registry.emplace<Position>( entity, i, -i );
        if ( i < 8 ) {
            registry.emplace<Tag>( entity, ( i * i ) % 6 );
        }

        if ( i == 3 ) {
            entity_3 = entity;
        } else if ( i == 8 ) {
            entity_8 = entity;
        }
    }
    // registry.sort<Tag>( []( const Tag &lhs, const Tag &rhs ) { return lhs.id > rhs.id; } );
    print_group( "Before" );
    registry.destroy( entity_3 );
    registry.destroy( entity_8 );

    // registry.sort<Tag>( []( const Tag &lhs, const Tag &rhs ) { return lhs.id > rhs.id; } );
    print_group( "After" );
    return 0;
};