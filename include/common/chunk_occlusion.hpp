#pragma once

#include <cstdint>

// Consecutive failed AABB probes before a chunk is treated as occluded (see
// ChunkOcclState::fails). >1 absorbs a stale in-flight result read after slot
// reuse, at the cost of one extra frame of overdraw on real occlusion.
#define OCCL_HIDE_FRAMES 2

// AABB occlusion-probe verdict state for one chunk (ChunkLoader::draw,
// normal pass only). Each frustum-visible chunk probes its [0,CHUNK_SIZE)
// bounding box against the opaque depth buffer once per frame
// (GL_ANY_SAMPLES_PASSED). fails counts consecutive failed probes; at
// OCCL_HIDE_FRAMES every render layer skips the chunk — conservative, since
// its content is confined to the box. Hidden chunks keep probing, so a
// reveal un-hides them the frame the box reappears. query is lazily created
// on the GL thread and persists across slot reuse.
//
// Verdict validity is split into two generations plus a coverage bit. A
// position generation (camera eye moved) invalidates everything: depth
// changed under the probe, so stale results prove nothing. A rotation
// generation only invalidates verdicts earned while the chunk was NOT fully
// inside the frustum: for a fully-inside chunk the whole box was tested, so
// the verdict is rotation-invariant at a fixed eye position, but a chunk
// straddling the frustum edge was only probed on its on-screen part and
// rotating can reveal an untested part. record() therefore drops partial
// results issued under an older rotation, keeps full results for any
// rotation at the same position, and hides() requires the streak to be
// valid for the current rotation (fails_full, or earned under it).
struct ChunkOcclState {
    unsigned int query = 0;   // GL query, lazily created, persists across slot reuse
    int pending = 0;
    int fails = 0;            // consecutive failed probes in the current streak
    long probe_gen = 0;       // position gen the in-flight probe was issued under
    long probe_rot = 0;       // rotation gen the in-flight probe was issued under
    uint8_t probe_full = 0;   // chunk was fully inside the frustum at issue
    long fails_gen = 0;       // position gen of the current streak
    long fails_rot = 0;       // rotation gen the streak's partial fails are valid under
    uint8_t fails_full = 0;   // streak contains a full-frustum fail
    uint8_t in_full = 0;      // set by calculate_cull (normal pass): sphere fully inside all 6 planes
    void reset_streak( ) {
        this->fails = 0;
        this->fails_full = 0;
    }
    void mark_probe_issued( long gen, long rot ) {
        this->pending = 1;
        this->probe_gen = gen;
        this->probe_rot = rot;
        this->probe_full = this->in_full;
    }
    // Called when a finished query result is retired; clears pending. passed
    // is the GL_ANY_SAMPLES_PASSED result; gen/rot are the current
    // position/rotation generations.
    void record( bool passed, long gen, long rot ) {
        this->pending = 0;
        if ( this->probe_gen != gen ) {
            return; // issued before the camera moved: discard
        }
        if ( this->fails_gen != gen ) {
            this->reset_streak( );
            this->fails_gen = gen;
            this->fails_rot = rot;
        }
        if ( passed ) {
            this->reset_streak( );
            this->fails_rot = rot;
        } else if ( this->probe_full ) {
            this->fails++;
            this->fails_full = 1; // whole box tested: valid for any rotation at this position
        } else if ( this->probe_rot == rot ) {
            if ( !this->fails_full && this->fails_rot != rot ) {
                this->fails = 0;
            }
            this->fails++;
            this->fails_rot = rot;
        }
        // else: partial probe from an older rotation proves nothing about the
        // now-visible part — drop it
    }
    bool hides( long gen, long rot ) const {
        return this->fails_gen == gen && this->fails >= OCCL_HIDE_FRAMES && ( this->fails_full || this->fails_rot == rot );
    }
};
