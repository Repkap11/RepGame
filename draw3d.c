#include "draw3d.h"
#include <GL/glew.h>

#include "textures.h"

// void draw3d_cube( ) {

//     glColor3f( 1.0f, 1.0f, 1.0f );

//     glBindTexture( GL_TEXTURE_2D, textures_getDirtTexture( ) );
//     // FRONT
//     glBegin( GL_TRIANGLES );
//     {

//         glTexCoord2f( 0, 1 );
//         glVertex3f( 1.0, 1.0, 0.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 0.0, 0.0, 0.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//     }
//     glEnd( );

//     // BACK
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 1.0 );
//         glTexCoord2f( 0, 1 );
//         glVertex3f( 1.0, 1.0, 1.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 1.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {

//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 1.0 );
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 0.0, 0.0, 1.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 1.0 );
//     }
//     glEnd( );

//     // RIGHT
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//         glTexCoord2f( 0, 1 );
//         glVertex3f( 1.0, 1.0, 0.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 1.0, 1.0, 1.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {

//         glTexCoord2f( 1, 1 );
//         glVertex3f( 1.0, 1.0, 1.0 );
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 1.0, 0.0, 1.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//     }
//     glEnd( );

//     // LEFT
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 0.0, 0.0, 1.0 );
//         glTexCoord2f( 0, 1 );
//         glVertex3f( 0.0, 1.0, 1.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {

//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 0.0, 0.0, 0.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 0.0, 0.0, 1.0 );
//     }
//     glEnd( );

//     // BOTTOM
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//         glTexCoord2f( 0, 1 );
//         glVertex3f( 1.0, 0.0, 1.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 0.0, 1.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {

//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 0.0, 1.0 );
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 0.0, 0.0, 0.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 0.0, 0.0 );
//     }
//     glEnd( );

//     glBindTexture( GL_TEXTURE_2D, textures_getGrassTexture( ) );
//     // TOP
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 1.0, 1.0 );
//         glTexCoord2f( 0, 1 );
//         glVertex3f( 1.0, 1.0, 0.0 );
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//     }
//     glEnd( );
//     glBegin( GL_TRIANGLES );
//     {
//         glTexCoord2f( 1, 1 );
//         glVertex3f( 0.0, 1.0, 0.0 );
//         glTexCoord2f( 1, 0 );
//         glVertex3f( 0.0, 1.0, 1.0 );
//         glTexCoord2f( 0, 0 );
//         glVertex3f( 1.0, 1.0, 1.0 );
//     }
//     glEnd( );
// }

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
    gluSphere( sphere, 1.0, 20, 20 );
    glEndList( );
    gluDeleteQuadric( sphere );
    glBindTexture( GL_TEXTURE_2D, textures_getSkyTexture( ) );
    glCallList( mysphereID );
    glFrontFace( GL_CCW );
}
