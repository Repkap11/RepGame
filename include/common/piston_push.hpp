#pragma once

struct RepGameState;

// Post-integration piston-contact pass. Pushes only entities this client
// simulates, then consumes any one-shot landing motions queued by
// BlockBodies. Runs before entity network state collection so the corrected
// positions stream through the existing sync paths.
void repgame_piston_push_entities( RepGameState &state );
