#pragma once


#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "binary_blobs.hpp"
#include "common/renderer/texture.hpp"

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_ANDROID )
typedef BinaryBlob ShaderSourceData;
#define MK_SHADER( name ) MK_BLOB( src_shaders, name, glsl )

#elif defined( REPGAME_WASM )
typedef struct {
    const char *path;
} ShaderSourceData;
#define MK_SHADER( name ) ShaderSourceData name = { .path = #name ".glsl" }
#endif

class Shader {

    unsigned int m_RendererId;
    // name -> glGetUniformLocation result, -1 included so each missing
    // uniform warns once per program; cleared whenever the program changes.
    std::unordered_map<std::string, int> m_UniformLocations;
    std::string m_LookupKey; // reused lookup buffer: no per-call allocation

  public:
    int get_uniform_location( const char *name );
    void init( const ShaderSourceData *vertex, const ShaderSourceData *fragment );
    void bind( ) const;
    void unbind( ) const;
    void destroy( );
    void set_uniform4f( const char *name, float f0, float f1, float f2, float f3 );
    void set_uniform3f( const char *name, float f0, float f1, float f2 );
    void set_uniform2f( const char *name, float f0, float f1 );
    void set_uniform1fv( const char *name, float *f, int count );
    void set_uniform1f( const char *name, float f );
    void set_uniform1i( const char *name, int i );
    void set_uniform1i_texture( const char *name, const Texture &texture );
    void set_uniform1ui( const char *name, unsigned int i );
    void set_uniform_mat4f( const char *name, const glm::mat4 &mat );
};