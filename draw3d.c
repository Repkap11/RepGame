
#include "draw3d.h"
#include <GL/gl.h>
#include <GL/glut.h>

#include "block.h"
#include "textures.h"

void draw3d_cube( ) {
    draw3d_cube_parts( 1, 1, 1, 1, 1, 1, 1 );
}
void draw3d_cube_parts( int top, int bottom, int left, int right, int front, int back, int type ) {

    BlockTextureMap btm;
    if ( type == 1 ) {
        btm.top = textures_getGrassTexture( );
        btm.side = textures_getGrassSideTexture( );
        btm.bottom = textures_getDirtTexture( );
        btm.alpha = 1.0f;
        btm.height = 1.0f;
    }

    if ( type == 2 ) {
        btm.top = textures_getWaterTexture( );
        btm.side = textures_getWaterTexture( );
        btm.bottom = textures_getWaterTexture( );
        btm.alpha = 0.5f;
        btm.height = 1.0f;
    }

    glColor4f( 1.0f, 1.0f, 1.0f, btm.alpha );
    if ( top ) {
        glBindTexture( GL_TEXTURE_2D, btm.side );
    } else {
        glBindTexture( GL_TEXTURE_2D, btm.bottom );
    }
    if ( left ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, btm.height, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glEnd( );
    }

    if ( right ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, btm.height, 1.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 1.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 1.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glEnd( );
    }

    if ( front ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, btm.height, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 1.0, btm.height, 1.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 1.0, btm.height, 1.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glEnd( );
    }

    if ( back ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 0.0, btm.height, 1.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glEnd( );
    }

    glBindTexture( GL_TEXTURE_2D, btm.bottom );
    if ( bottom ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glEnd( );
    }

    glBindTexture( GL_TEXTURE_2D, btm.top );
    if ( top ) {
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, btm.height, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, btm.height, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, btm.height, 0.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, btm.height, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, btm.height, 1.0 );
        glEnd( );
    }
}

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
    glBindTexture( GL_TEXTURE_2D, textures_getSkyTexture( ) );
    glCallList( mysphereID );
    glFrontFace( GL_CCW );
}
