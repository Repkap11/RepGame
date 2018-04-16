
#include "draw3d.h"
#include <GL/gl.h>
#include <GL/glut.h>

#include "textures.h"

void draw3d_sphere( ) {

    // int list = 0;

    // glNewList( list, GL_COMPILE );

    // GLUquadricObj *sphere = gluNewQuadric( );
    // // gluQuadricDrawStyle( sphere, GLU_FILL );
    // // gluQuadricNormals( sphere, GLU_SMOOTH );
    // // gluQuadricOrientation( sphere, GLU_OUTSIDE );
    // gluQuadricTexture( sphere, GL_TRUE );

    // // glColor3f( 0.0, 1.0, 0.0 );
    // gluSphere( sphere, 1, 16, 16 );

    // glEndList( );
    glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
    glFrontFace( GL_CW );
    GLUquadricObj *sphere = NULL;
    sphere = gluNewQuadric( );
    // gluQuadricDrawStyle( sphere, GLU_FILL );
    gluQuadricTexture( sphere, 1 );
    // gluQuadricNormals( sphere, GLU_SMOOTH );
    // Making a display list
    int mysphereID = glGenLists( 1 );
    glNewList( mysphereID, GL_COMPILE );
    gluSphere( sphere, 1.0, 10, 10 );
    glEndList( );
    gluDeleteQuadric( sphere );
    Textures textures;
    textures_init_sky( &textures );
    Texture skyTexture = textures_get_texture(&textures, 0);
    texture_bind(&skyTexture);
    glCallList( mysphereID );
    glFrontFace( GL_CCW );
}
