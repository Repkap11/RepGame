
#include "draw3d.h"
#include <GL/gl.h>
#include <GL/glut.h>

#include "RepGame.h"
#include "block.h"

void block_draw( Block *block ) {
    BlockDefinition *blockDef = block->blockDef;
    glScalef( BLOCK_SCALE, BLOCK_SCALE, BLOCK_SCALE );
    glColor4f( 1.0f, 1.0f, 1.0f, blockDef->alpha );
    float height = blockDef->height;
    if ( !block->draw_sides.top && blockDef->special_grass_logic ) {
        glBindTexture( GL_TEXTURE_2D, blockDef->textures.bottom );
    } else {
        glBindTexture( GL_TEXTURE_2D, blockDef->textures.side );
    }
    if ( block->draw_sides.left ) {
        glNormal3f( 0, 0, 1 );
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, height, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glEnd( );
    }

    if ( block->draw_sides.right ) {
        glNormal3f( 0, 0, -1 );
        glBegin( GL_TRIANGLES );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, height, 1.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 1.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 1.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glEnd( );
    }

    if ( block->draw_sides.front ) {
        glBegin( GL_TRIANGLES );
        glNormal3f( 1, 0, 0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, height, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 1.0, height, 1.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 1.0, height, 1.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 1.0, 0.0, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, 0.0, 0.0 );
        glEnd( );
    }

    if ( block->draw_sides.back ) {
        glBegin( GL_TRIANGLES );
        glNormal3f( -1, 0, 0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 0.0, height, 1.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, 0.0, 0.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 0.0, 0.0, 1.0 );
        glEnd( );
    }

    glBindTexture( GL_TEXTURE_2D, blockDef->textures.bottom );
    if ( block->draw_sides.bottom ) {
        glBegin( GL_TRIANGLES );
        glNormal3f( 0, -1, 0 );
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

    glBindTexture( GL_TEXTURE_2D, blockDef->textures.top );
    if ( block->draw_sides.top ) {
        glBegin( GL_TRIANGLES );
        glNormal3f( 0, 1, 0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, height, 1.0 );
        glTexCoord2f( 0, 1 );
        glVertex3f( 1.0, height, 0.0 );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glEnd( );

        glBegin( GL_TRIANGLES );
        glTexCoord2f( 1, 1 );
        glVertex3f( 0.0, height, 0.0 );
        glTexCoord2f( 1, 0 );
        glVertex3f( 0.0, height, 1.0 );
        glTexCoord2f( 0, 0 );
        glVertex3f( 1.0, height, 1.0 );
        glEnd( );
    }
}