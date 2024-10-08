#include "common/renderer/shader.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/utils/file_utils.hpp"
#include "common/RepGame.hpp"

#define SHADER_BLOCK_SIZE 512
#define FULL_PATH_SIZE 8192

char *shaderLoadSourceFromFile( const char *filePath ) {
    FILE *fp;
    char buf[ SHADER_BLOCK_SIZE ];
    char *source = NULL;
    size_t tmp, sourceLength = 0;

    /* open file */
    char *dir = getRepGamePath( );
    char fullPath[ FULL_PATH_SIZE ];
    snprintf( fullPath, FULL_PATH_SIZE, "%s/src/shaders/%s", dir, filePath );
    free( dir );
    pr_debug( "shader path:%s", fullPath );
    fp = fopen( fullPath, "r" );
    if ( !fp ) {
        pr_debug( "shaderLoadSource(): Unable to open %s for reading\n", fullPath );
        return NULL;
    }

    /* read the entire file into a string */
    while ( ( tmp = fread( buf, 1, SHADER_BLOCK_SIZE, fp ) ) > 0 ) {
        char *newSource = ( char * )malloc( sourceLength + tmp + 1 );
        if ( !newSource ) {
            pr_debug( "shaderLoadSource(): malloc failed\n" );
            if ( source ) {
                free( source );
            }
            fclose( fp );
            return NULL;
        }

        if ( source ) {
            memcpy( newSource, source, sourceLength );
            free( source );
        }
        memcpy( newSource + sourceLength, buf, tmp );

        source = newSource;
        sourceLength += tmp;
    }

    /* close the file and null terminate the string */
    fclose( fp );
    if ( source ) {
        source[ sourceLength ] = '\0';
    }

    return source;
}

unsigned int shaderCompile( int type, const ShaderSourceData *shader_data ) {
    unsigned int shader;
    int result;

    shader = glCreateShader( type );

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
    glShaderSource( shader, 1, ( const char ** )&shader_data->source, &shader_data->length );
#elif defined( REPGAME_WASM )
    char *source = shaderLoadSourceFromFile( shader_data->path );
    int length = strlen( source );
    glShaderSource( shader, 1, ( const char ** )&source, &length );
#elif defined( REPGAME_ANDROID )
    char *source = repgame_android_getShaderString( shader_data->resource_path );
    int length = strlen( source );
    glShaderSource( shader, 1, ( const char ** )&source, &length );
#endif
    glCompileShader( shader );

    /* make sure the compilation was successful */
    glGetShaderiv( shader, GL_COMPILE_STATUS, &result );
    if ( result == GL_FALSE ) {
        char *log;
        int log_length;

        /* get the shader info log */
        glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &log_length );
        log = ( char * )malloc( log_length );
        glGetShaderInfoLog( shader, log_length, &result, log );

        /* print an error message and the info log */
        pr_debug( "shaderCompileFromFile(): Unable to compile %s\n", log );
        free( log );

        glDeleteShader( shader );
        exit( 1 );
        return 0;
    }

    return shader;
}

void shaderAttach( unsigned int program, GLenum type, const ShaderSourceData *shader_source ) {
    GLuint shader = shaderCompile( type, shader_source );
    if ( shader != 0 ) {
        glAttachShader( program, shader );
        glDeleteShader( shader );
    }
}

unsigned int shaders_compile( const ShaderSourceData *vertex_path, const ShaderSourceData *fragment_path ) {
    /* create program object and attach shaders */
    unsigned int g_program;

    /* link the program and make sure that there were no errors */

    int result = 0;
    int try_counts = 0;
    while ( try_counts < 5 ) {
        try_counts++;
        g_program = glCreateProgram( );
        shaderAttach( g_program, GL_VERTEX_SHADER, vertex_path );
        shaderAttach( g_program, GL_FRAGMENT_SHADER, fragment_path );
        glLinkProgram( g_program );
        glValidateProgram( g_program );
        glGetProgramiv( g_program, GL_LINK_STATUS, &result );
        if ( result == GL_FALSE ) {
            GLint length;
            char *log;

            // get the program info log
            glGetProgramiv( g_program, GL_INFO_LOG_LENGTH, &length );
            if ( length < 100 ) {
                length = 100;
            }
            log = ( char * )malloc( length + 1 );
            glGetProgramInfoLog( g_program, length, &result, log );
            log[ length ] = 0;

            /* print an error message and the info log */
            pr_debug( "sceneInit(): Program linking failed: %d:%s\n", length, log );
            free( log );

            /* delete the program */
            glDeleteProgram( g_program );
            g_program = 0;
        } else {
            if ( try_counts != 1 ) {
                pr_debug( "Program good after:%d", try_counts );
            }
            return g_program;
        }
    }
    pr_debug( "Too many trys linking shader" );
    exit( 1 );
    return g_program;
}

void Shader::init( const ShaderSourceData *vertex, const ShaderSourceData *fragment ) {
    this->m_RendererId = shaders_compile( vertex, fragment );
}

void Shader::bind( ) const {
    glUseProgram( this->m_RendererId );
}

void Shader::unbind( ) const {
    glUseProgram( 0 );
}

void Shader::destroy( ) {
    glDeleteProgram( this->m_RendererId );
    this->m_RendererId = 0;
}

int printedUniform = 0;
int Shader::get_uniform_location( const char *name ) {
    // TODO use cache
    int result = glGetUniformLocation( this->m_RendererId, name );
    if ( result == -1 ) {
        if ( !printedUniform ) {
            printedUniform = 1;
            pr_debug( "Shader uniform not found. Name:%s", name );
            // exit(1);
        }
    }
    return result;
}

void Shader::set_uniform4f( const char *name, float f0, float f1, float f2, float f3 ) {
    this->bind( );
    glUniform4f( get_uniform_location( name ), f0, f1, f2, f3 );
}

void Shader::set_uniform3f( const char *name, float f0, float f1, float f2 ) {
    this->bind( );
    glUniform3f( get_uniform_location( name ), f0, f1, f2 );
}

void Shader::set_uniform1fv( const char *name, float *f, int count ) {
    this->bind( );
    glUniform1fv( get_uniform_location( name ), count, f );
}

void Shader::set_uniform1f( const char *name, float f ) {
    this->bind( );
    glUniform1f( get_uniform_location( name ), f );
}

void Shader::set_uniform1i_texture( const char *name, const Texture &texture ) {
    this->bind( );
    glUniform1i( get_uniform_location( name ), texture.slot );
}

void Shader::set_uniform1i( const char *name, int i ) {
    this->bind( );
    glUniform1i( get_uniform_location( name ), i );
}

void Shader::set_uniform1ui( const char *name, unsigned int i ) {
    this->bind( );
    glUniform1ui( get_uniform_location( name ), i );
}

void Shader::set_uniform_mat4f( const char *name, const glm::mat4 &mat ) {
    this->bind( );
    glUniformMatrix4fv( get_uniform_location( name ), 1, GL_FALSE, &mat[ 0 ][ 0 ] );
}