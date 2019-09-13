#ifndef HEADER_SHADER_H
#define HEADER_SHADER_H

#include <glm.hpp>
#include "binary_blobs.hpp"

typedef struct {
    unsigned int m_RendererId; //
} Shader;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
typedef BinaryBlob ShaderSourceData;
#define MK_SHADER( name ) MK_BLOB( src_shaders, name, glsl )

#elif defined( REPGAME_WASM )
typedef struct {
    const char *path;
} ShaderSourceData;
#define MK_SHADER( name ) ShaderSourceData name = {.path = #name ".glsl"}

#elif defined( REPGAME_ANDROID )
typedef struct {
    const char *resource_path;
} ShaderSourceData;
#define MK_SHADER( name ) ShaderSourceData name = {.resource_path = #name ".glsl"}
#endif

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )

#else

#endif

void shader_init( Shader *shader, const ShaderSourceData *vertex, const ShaderSourceData *fragment );
void shader_bind( const Shader *shader );
void shader_unbind( const Shader *shader );
void shader_destroy( Shader *shader );
void shader_set_uniform4f( Shader *shader, const char *name, float f0, float f1, float f2, float f3 );
void shader_set_uniform3f( Shader *shader, const char *name, float f0, float f1, float f2 );
void shader_set_uniform1f( Shader *shader, const char *name, float f );
void shader_set_uniform1i( Shader *shader, const char *name, int i );
void shader_set_uniform1ui( Shader *shader, const char *name, unsigned int i );
void shader_set_uniform_mat4f( Shader *shader, const char *name, const glm::mat4 &mat );
#endif