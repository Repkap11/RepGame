#pragma once

#include <entt/entity/registry.hpp>

struct Position {
    int x;
    int y;
};

struct Tag {
    int id;
};

struct ECS {
    entt::registry registry;
    int test_ecs( );

  private:
    void print_group( const std::string &msg );
    void print_view( const std::string &msg );
};