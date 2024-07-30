#include "common/utils/ecs.hpp"

struct position {
    float x;
    float y;
};

struct velocity {
    float dx;
    float dy;
};

int entt_test( ) {
    // we can create a entt::registry to store our entities
    entt::registry registry;

    // we'll create 10 entities
    for ( std::size_t i = 0; i < 10; ++i ) {
        // to create an entity, use the create function
        const auto entity = registry.create( );
        // and by using emplace with a specific component
        // we add a component to an entity and forward all their arguments
        registry.emplace<position>( entity, i * 1.f, i * 1.f );
        // if we have a even number, we'll also add a velocity component to it
        if ( i % 2 == 0 ) {
            registry.emplace<velocity>( entity, i * .1f, i * .1f );
        }
    }

    // now we can create a view, which is a partial registry
    // pass in all the components we need and in the view we'll have
    // all entities with the given components
    auto view = registry.view<const position, velocity>( );

    // now there are three different options to loop through the entities from our view
    // 1.: a basic lambda which will be called with the given components
    // note: the lambda arguments need to match the components with which we create this view
    view.each( []( const auto &pos, auto &vel ) { /* ... */ } );

    // 2.: an extended lambda which also gives us the entity if we need it
    view.each( []( const auto entity, const auto &pos, auto &vel ) { /* ... */ } );

    // 3. a for loop by using structured bindings
    for ( auto [ entity, pos, vel ] : view.each( ) ) {
        // ...
    }

    // 4. a for loop with a forward iterator
    for ( auto entity : view ) {
        auto &vel = view.get<velocity>( entity );
        // ...
    }
}