#include "common/utils/frame_stats.hpp"

#include <algorithm>
#include <math.h>

void FrameTimeStats::add_frame( const float dt_s ) {
    if ( dt_s <= 0 ) {
        return;
    }
    if ( count_ == MAX_SAMPLES ) {
        window_sum_ -= samples_[ head_ ];
        head_ = ( head_ + 1 ) % MAX_SAMPLES;
        count_--;
    }
    samples_[ ( head_ + count_ ) % MAX_SAMPLES ] = dt_s;
    count_++;
    window_sum_ += dt_s;
    // Keep the smallest suffix covering >= RETAIN_S so an fps drop shrinks
    // the sample count instead of dragging old history along.
    while ( count_ > 1 && window_sum_ - samples_[ head_ ] >= RETAIN_S ) {
        window_sum_ -= samples_[ head_ ];
        head_ = ( head_ + 1 ) % MAX_SAMPLES;
        count_--;
    }
    since_refresh_ += dt_s;
    if ( count_ == 1 || since_refresh_ >= RECOMPUTE_S ) {
        summary_ = compute( );
        since_refresh_ = 0;
    }
}

int FrameTimeStats::suffix_len( const float window_s ) const {
    double sum = 0;
    int n = 0;
    while ( n < count_ && sum < window_s ) {
        sum += samples_[ ( head_ + count_ - 1 - n + MAX_SAMPLES ) % MAX_SAMPLES ];
        n++;
    }
    return n;
}

FrameTimeStats::Summary FrameTimeStats::compute( ) const {
    Summary s;
    s.frames = count_;
    if ( count_ == 0 ) {
        return s;
    }

    // avg over the newest ~AVG_WINDOW_S of frames.
    const int n_avg = suffix_len( AVG_WINDOW_S );
    double sum_avg = 0;
    for ( int i = 0; i < n_avg; i++ ) {
        sum_avg += samples_[ ( head_ + count_ - 1 - i + MAX_SAMPLES ) % MAX_SAMPLES ];
    }
    s.avg_fps = static_cast<float>( n_avg / sum_avg );
    s.avg_ms = static_cast<float>( 1000.0 * sum_avg / n_avg );

    // "X% low" over the newest ~window_s of frames: k = ceil(n * X) of
    // that suffix, low = k / sum of the k largest durations.
    const auto low_fps = [ this ]( const float window_s, const float frac ) {
        const int n = suffix_len( window_s );
        std::vector<float> sorted;
        sorted.reserve( n );
        for ( int i = 0; i < n; i++ ) {
            sorted.push_back( samples_[ ( head_ + count_ - 1 - i + MAX_SAMPLES ) % MAX_SAMPLES ] );
        }
        const int k = std::max( 1, static_cast<int>( ceilf( n * frac ) ) );
        std::nth_element( sorted.begin( ), sorted.begin( ) + k, sorted.end( ), std::greater<float>( ) );
        double slow_sum = 0;
        for ( int i = 0; i < k; i++ ) {
            slow_sum += sorted[ i ];
        }
        return static_cast<float>( k / slow_sum );
    };
    s.low1_fps = low_fps( LOW1_WINDOW_S, 0.01f );
    s.low01_fps = low_fps( LOW01_WINDOW_S, 0.001f );
    return s;
}
