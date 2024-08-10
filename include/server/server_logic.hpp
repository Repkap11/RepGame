#pragma once

#include "server/server.hpp"

void server_logic_on_client_connected( int client_fd );
void server_logic_on_client_message( int client_fd, NetPacket *packet );
void server_logic_on_client_disconnected( int client_fd );