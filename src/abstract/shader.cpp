#include "abstract/shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_utils.h"
#include "RepGame.h"

char *shaderLoadSource( const char *filePath ) {
    const size_t blockSize = 512;
    FILE *fp;
    char buf[ blockSize ];
    char *source = NULL;
    size_t tmp, sourceLength = 0;

    /* open file */
    char *dir = getRepGamePath( );
    char fullPath[ BUFSIZ ];
    sprintf( fullPath, "%s%s%s", dir, "/", filePath );
    pr_debug( "shader path:%s", fullPath );
    fp = fopen( fullPath, "r" );
    if ( !fp ) {
        pr_debug( "shaderLoadSource(): Unable to open %s for reading\n", filePath );
        return NULL;
    }

    /* read the entire file into a string */
    while ( ( tmp = fread( buf, 1, blockSize, fp ) ) > 0 ) {
        char *newSource = ( char * )malloc( sourceLength + tmp + 1 );
        if ( !newSource ) {
            pr_debug( "shaderLoadSource(): malloc failed\n" );
            if ( source )
                free( source );
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
    if ( source )
        source[ sourceLength ] = '\0';

    return source;
}

unsigned int shaderCompileFromFile( int type, const char *filePath ) {
    char *source;
    unsigned int shader;
    int length, result;

    /* get shader source */
    source = shaderLoadSource( filePath );
    if ( !source )
        return 0;

    /* create shader object, set the source, and compile */
    shader = glCreateShader( type );
    length = strlen( source );
    glShaderSource( shader, 1, ( const char ** )&source, &length );
    glCompileShader( shader );
    free( source );

    /* make sure the compilation was successful */
    glGetShaderiv( shader, GL_COMPILE_STATUS, &result );
    if ( result == GL_FALSE ) {
        char *log;

        /* get the shader info log */
        glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &length );
        log = ( char * )malloc( length );
        glGetShaderInfoLog( shader, length, &result, log );

        /* print an error message and the info log */
        pr_debug( "shaderCompileFromFile(): Unable to compile %s: %s\n", filePath, log );
        free( log );

        glDeleteShader( shader );
        return 0;
    }

    return shader;
}

void shaderAttachFromFile( unsigned int program, GLenum type, const char *filePath ) {
    GLuint shader = shaderCompileFromFile( type, filePath );
    if ( shader != 0 ) {
        glAttachShader( program, shader );
        glDeleteShader( shader );
    }
}

unsigned int shaders_compile( const char *vertex_path, const char *fragment_path ) {
    /* create program object and attach shaders */
    unsigned int g_program = glCreateProgram( );

    shaderAttachFromFile( g_program, GL_VERTEX_SHADER, vertex_path );
    shaderAttachFromFile( g_program, GL_FRAGMENT_SHADER, fragment_path );

    /* link the program and make sure that there were no errors */
    glLinkProgram( g_program );
    glValidateProgram( g_program );
    int result;
    glGetProgramiv( g_program, GL_LINK_STATUS, &result );
    if ( result == GL_FALSE ) {
        GLint length;
        char *log;

        // get the program info log
        glGetProgramiv( g_program, GL_INFO_LOG_LENGTH, &length );
        log = ( char * )malloc( length );
        glGetProgramInfoLog( g_program, length, &result, log );

        /* print an error message and the info log */
        pr_debug( "sceneInit(): Program linking failed: %s\n", log );
        free( log );

        /* delete the program */
        glDeleteProgram( g_program );
        g_program = 0;
    }
    return g_program;
}

void shader_init( Shader *shader ) {
    shader->m_RendererId = shaders_compile( "shaders/vertex.glsl", "shaders/fragment.glsl" );
}

void shader_bind( const Shader *shader ) {
    glUseProgram( shader->m_RendererId );
}

void shader_unbind( const Shader *shader ) {
    glUseProgram( 0 );
}

void shader_destroy( Shader *shader ) {
    glDeleteProgram( shader->m_RendererId );
    shader->m_RendererId = 0;
}

int printedUniform = 0;
int get_uniform_location( Shader *shader, const char *name ) {
    // TODO use cache
    int result = glGetUniformLocation( shader->m_RendererId, name );
    if ( result == -1 ) {
        if ( !printedUniform ) {
            printedUniform = 1;
            pr_debug( "Shader uniform not found. Name:%s", name );
        }
    }
    return result;
}

void shader_set_uniform4f( Shader *shader, const char *name, float f0, float f1, float f2, float f3 ) {
    shader_bind( shader );
    glUniform4f( get_uniform_location( shader, name ), f0, f1, f2, f3 );
}

void shader_set_uniform1i( Shader *shader, const char *name, int i ) {
    shader_bind( shader );
    glUniform1i( get_uniform_location( shader, name ), i );
}

void shader_set_uniform1ui( Shader *shader, const char *name, unsigned int i ) {
    shader_bind( shader );
    glUniform1ui( get_uniform_location( shader, name ), i );
}

void shader_set_uniform_mat4f( Shader *shader, const char *name, glm::mat4 &mat ) {
    shader_bind( shader );
    glUniformMatrix4fv( get_uniform_location( shader, name ), 1, GL_FALSE, &mat[ 0 ][ 0 ] );
}