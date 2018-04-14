#include <GL/glew.h>

#include <GL/glext.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders.h"
#include "file_utils.h"

uint shaders_compile( ) {
    /* create program object and attach shaders */
    uint g_program = glCreateProgram( );

    shaderAttachFromFile( g_program, GL_VERTEX_SHADER, "shaders/vertex.glsl" );
    shaderAttachFromFile( g_program, GL_FRAGMENT_SHADER, "shaders/fragment.glsl" );

    /* link the program and make sure that there were no errors */
    glLinkProgram( g_program );
    int result;
    glGetProgramiv( g_program, GL_LINK_STATUS, &result );
    if ( result == GL_FALSE ) {
        GLint length;
        char *log;

        // get the program info log
        glGetProgramiv( g_program, GL_INFO_LOG_LENGTH, &length );
        log = malloc( length );
        glGetProgramInfoLog( g_program, length, &result, log );

        /* print an error message and the info log */
        fprintf( stderr, "sceneInit(): Program linking failed: %s\n", log );
        free( log );

        /* delete the program */
        glDeleteProgram( g_program );
        g_program = 0;
    }
    return g_program;
}

void shaderAttachFromFile( GLuint program, GLenum type, const char *filePath ) {
    /* compile the shader */
    GLuint shader = shaderCompileFromFile( type, filePath );
    if ( shader != 0 ) {
        /* attach the shader to the program */
        glAttachShader( program, shader );

        /* delete the shader - it won't actually be
         * destroyed until the program that it's attached
         * to has been destroyed */
        glDeleteShader( shader );
    }
}

uint shaderCompileFromFile( int type, const char *filePath ) {
    char *source;
    uint shader;
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
        log = malloc( length );
        glGetShaderInfoLog( shader, length, &result, log );

        /* print an error message and the info log */
        fprintf( stderr, "shaderCompileFromFile(): Unable to compile %s: %s\n", filePath, log );
        free( log );

        glDeleteShader( shader );
        return 0;
    }

    return shader;
}
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
    pr_debug("shader path:%s",fullPath);
    fp = fopen( fullPath, "r" );
    if ( !fp ) {
        fprintf( stderr, "shaderLoadSource(): Unable to open %s for reading\n", filePath );
        return NULL;
    }

    /* read the entire file into a string */
    while ( ( tmp = fread( buf, 1, blockSize, fp ) ) > 0 ) {
        char *newSource = malloc( sourceLength + tmp + 1 );
        if ( !newSource ) {
            fprintf( stderr, "shaderLoadSource(): malloc failed\n" );
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