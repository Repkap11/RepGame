#pragma once

// ---------------------------------------------------------------------------
// wgsl_dialect.hpp — a C++/WGSL dual-target source dialect.
//
// Shared algorithm files (map_gen_fields*.hpp, perlin_noise_shared.hpp) are
// written once in this macro dialect so a single file serves every backend:
//
//   C++ (g++/clang) : MG_* expands to ordinary C++.
//   CUDA / HIP      : same C++ expansion plus the includer's MG_QUAL
//                     (__device__).
//   WGSL (WebGPU)   : the file is run through the C preprocessor with
//                     -DREPGAME_WGSL_GEN (SHADER_PP-style), and every MG_*
//                     expands to WGSL text. The output is a .wgsl compute
//                     shader — no hand-ported copy of the logic exists.
//
// Rules for code written in the dialect:
//   - Functions are declared with MG_FN0..MG_FN6 (arity-suffixed). All
//     parameters become `const T name` on C++ (refs collapse to const value)
//     and `name : T` on WGSL. Parameters are immutable on WGSL — never
//     assign to them; introduce a MG_VAR local instead.
//   - Locals: MG_LET (immutable), MG_VAR (mutable), MG_DECL (uninitialized).
//   - Casts must be explicit via MG_F32 / MG_I32 / MG_U32 — WGSL has no
//     implicit numeric conversions, so `y - terrainHeight` (int - float) is
//     a type error there but legal C++. MG_F32(y) compiles to the identical
//     (float)y conversion on the C++ side.
//   - No `?:` — WGSL lacks a ternary. Use MG_TERN(cond, a, b) which becomes
//     select(b, a, cond). WGSL select evaluates BOTH branches eagerly, so
//     both sides must be pure expressions safe to evaluate unconditionally
//     (this differs from C++'s short-circuit ternary — watch for divisions
//     or array lookups that are only valid on one branch).
//   - Math builtins go through MG_ABS / MG_FLOOR / MG_SQRT / MG_MIN / MG_MAX
//     / MG_CEIL / MG_FMOD (WGSL has no float `fmod`/`%`, MG_FMOD expands to
//     a - b*floor(a/b) which is exact for the values used here).
//   - Loops over ints: MG_FOR(i, init, cond, step). `i++`/`i--` are valid
//     statements in WGSL.
//   - No goto, no switch, no local arrays other than via MG_TABLE/fields
//     added for the purpose.
//   - Types usable in signatures/decls must be listed in the MG_T_* table
//     below (float, int, bool, unsigned, BlockID, MapGenColumn, BlockCell).
// ---------------------------------------------------------------------------

#if defined( REPGAME_WGSL_GEN )

// ---------- WGSL expansion -------------------------------------------------
// Type-name mapping: MG_T(token) -> WGSL spelling. Add entries here when the
// dialect grows a new parameter/local type.
#define MG_T( t ) MG_WT_##t
#define MG_WT_float f32
#define MG_WT_int i32
#define MG_WT_unsigned u32
#define MG_WT_bool bool
#define MG_WT_BlockID u32
#define MG_WT_BlockCell u32
#define MG_WT_MapGenColumn MapGenColumn
#define MG_WT_GPUBlockDef GPUBlockDef

#define MG_FN0( r, n ) fn n() -> MG_T( r )
#define MG_FN1( r, n, t1, a1 ) fn n( a1 : MG_T( t1 ) ) -> MG_T( r )
#define MG_FN2( r, n, t1, a1, t2, a2 ) fn n( a1 : MG_T( t1 ), a2 : MG_T( t2 ) ) -> MG_T( r )
#define MG_FN3( r, n, t1, a1, t2, a2, t3, a3 ) \
    fn n( a1 : MG_T( t1 ), a2 : MG_T( t2 ), a3 : MG_T( t3 ) ) -> MG_T( r )
#define MG_FN4( r, n, t1, a1, t2, a2, t3, a3, t4, a4 ) \
    fn n( a1 : MG_T( t1 ), a2 : MG_T( t2 ), a3 : MG_T( t3 ), a4 : MG_T( t4 ) ) -> MG_T( r )
#define MG_FN5( r, n, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5 ) \
    fn n( a1 : MG_T( t1 ), a2 : MG_T( t2 ), a3 : MG_T( t3 ), a4 : MG_T( t4 ), a5 : MG_T( t5 ) ) -> MG_T( r )
#define MG_FN6( r, n, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6 ) \
    fn n( a1 : MG_T( t1 ), a2 : MG_T( t2 ), a3 : MG_T( t3 ), a4 : MG_T( t4 ), a5 : MG_T( t5 ), a6 : MG_T( t6 ) ) -> MG_T( r )

#define MG_LET( t, n, e ) let n : MG_T( t ) = e;
#define MG_VAR( t, n, e ) var n : MG_T( t ) = e;
#define MG_DECL( t, n ) var n : MG_T( t );

#define MG_F32( e ) f32( e )
#define MG_I32( e ) i32( e )
#define MG_U32( e ) u32( e )
#define MG_BOOL( e ) bool( e )

#define MG_TERN( c, a, b ) select( ( b ), ( a ), ( c ) )
// WGSL shift amounts must be u32; the operand keeps its own signedness.
#define MG_SHL( a, b ) ( ( a ) << u32( b ) )
#define MG_SHR( a, b ) ( ( a ) >> u32( b ) )
#define MG_ABS( e ) abs( e )
#define MG_FLOOR( e ) floor( e )
#define MG_CEIL( e ) ceil( e )
#define MG_SQRT( e ) sqrt( e )
#define MG_MIN( a, b ) min( ( a ), ( b ) )
#define MG_MAX( a, b ) max( ( a ), ( b ) )
#define MG_FMOD( a, b ) ( ( a ) - ( b ) * floor( ( a ) / ( b ) ) )

#define MG_FOR( i, init, cond, step ) for ( var i : i32 = init; cond; step )
#define MG_HASH( e ) hash[ u32( e ) % 256u ]

#else // !REPGAME_WGSL_GEN — C++ expansion (host g++, nvcc, hipcc)

#define MG_T( t ) t

#define MG_FN0( r, n ) MG_QUAL r n()
#define MG_FN1( r, n, t1, a1 ) MG_QUAL r n( const t1 a1 )
#define MG_FN2( r, n, t1, a1, t2, a2 ) MG_QUAL r n( const t1 a1, const t2 a2 )
#define MG_FN3( r, n, t1, a1, t2, a2, t3, a3 ) MG_QUAL r n( const t1 a1, const t2 a2, const t3 a3 )
#define MG_FN4( r, n, t1, a1, t2, a2, t3, a3, t4, a4 ) MG_QUAL r n( const t1 a1, const t2 a2, const t3 a3, const t4 a4 )
#define MG_FN5( r, n, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5 ) \
    MG_QUAL r n( const t1 a1, const t2 a2, const t3 a3, const t4 a4, const t5 a5 )
#define MG_FN6( r, n, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6 ) \
    MG_QUAL r n( const t1 a1, const t2 a2, const t3 a3, const t4 a4, const t5 a5, const t6 a6 )

#define MG_LET( t, n, e ) const t n = e;
#define MG_VAR( t, n, e ) t n = e;
#define MG_DECL( t, n ) t n;

#define MG_F32( e ) ( (float)( e ) )
#define MG_I32( e ) ( (int)( e ) )
#define MG_U32( e ) ( ( unsigned int )( e ) )
#define MG_BOOL( e ) ( (bool)( e ) )

#define MG_TERN( c, a, b ) ( ( c ) ? ( a ) : ( b ) )
#define MG_SHL( a, b ) ( ( a ) << ( b ) )
#define MG_SHR( a, b ) ( ( a ) >> ( b ) )
#define MG_ABS( e ) fabsf( e )
#define MG_FLOOR( e ) floorf( e )
#define MG_CEIL( e ) ceilf( e )
#define MG_SQRT( e ) sqrtf( e )
#define MG_MIN( a, b ) fminf( ( a ), ( b ) )
#define MG_MAX( a, b ) fmaxf( ( a ), ( b ) )
#define MG_FMOD( a, b ) fmodf( ( a ), ( b ) )

#define MG_FOR( i, init, cond, step ) for ( int i = init; cond; step )
#define MG_HASH( e ) hash[ ( unsigned int )( e ) % 256 ]

#endif
