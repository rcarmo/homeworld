// =============================================================================
//  BTG.c
//  - Background Tool of the Geeks; load and render the BTG backgrounds
// =============================================================================
//  Copyright Relic Entertainment, Inc. All rights reserved.
//  Created 4/27/1998 by khent
// =============================================================================

#include "BTG.h"

#include "Camera.h"
#include "Clipper.h"
#include "Debug.h"
#include "FastMath.h"
#include "File.h"
#include "glcaps.h"
#include "glinc.h"
#include "main.h"
#include "mainrgn.h"
#include "Memory.h"
#include "render.h"
#include "rglu.h"
#include "texreg.h"
#include "Universe.h"
#include "Vector.h"

#ifdef HW_BUILD_FOR_DEBUGGING
    #define BTG_VERBOSE_LEVEL  3    // print extra info
#else
    #define BTG_VERBOSE_LEVEL  0
#endif


// -----
// data
// -----

#define M_PI_F 3.1415926535f

static ubyte lastbg[4] = {255,255,255,0};

char btgLastBackground[128] = "";

static real32 btgThetaOffset;
static real32 btgPhiOffset;

static GLfloat _bgColor[4];
static GLubyte _bgByte[4];

static sdword btgFade = 255;

typedef struct btgTransVertex
{
    GLubyte red, green, blue, alpha;
    vector  position;
} btgTransVertex;

typedef struct btgTransStar
{
    vector ul, ll, lr, ur;
    vector mid;
} btgTransStar;

static btgHeader*      btgHead = NULL;
static btgVertex*      btgVerts = NULL;
static btgStar*        btgStars = NULL;
static btgPolygon*     btgPolys = NULL;
static btgTransVertex* btgTransVerts = NULL;
static btgTransStar*   btgTransStars = NULL;
static udword*         btgIndices = NULL;

typedef struct starTex
{
    char filename[48];
    udword glhandle;
    sdword width, height;
    struct starTex* next;
} starTex;

static starTex* texList = NULL;


typedef struct tagTGAFileHeader
{
    unsigned char idLength;
    unsigned char colorMapType;
    unsigned char imageType;
    // Color Map information.
    unsigned short colorMapStartIndex;
    unsigned short colorMapNumEntries;
    unsigned char colorMapBitsPerEntry;
    // Image Map information.
    signed short imageOffsetX;
    signed short imageOffsetY;
    unsigned short imageWidth;
    unsigned short imageHeight;
    unsigned char pixelDepth;
    unsigned char imageDescriptor;
} TGAFileHeader;


// -----
// code
// -----

#define fsin(A) (real32)sin((real64)(A))
#define fcos(A) (real32)cos((real64)(A))

/*-----------------------------------------------------------------------------
    Name        : btgStartup
    Description : initialize the btg subsystem
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgStartup()
{
    btgReset();
}

/*-----------------------------------------------------------------------------
    Name        : btgReset
    Description : reset the btg subsystem
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgReset()
{
    sdword numStars;

    lastbg[0] = 255;
    lastbg[1] = 255;
    lastbg[2] = 255;
    lastbg[3] = 0;

    btgFade = 255;

    btgThetaOffset = 0.0f;
    btgPhiOffset = 0.0f;

    universe.backgroundColor = 0;
    rndSetClearColor(colRGBA(0,0,0,255));

    if (texList != NULL)
    {
        starTex* ptex = texList;
        starTex* temptex;

        while (ptex != NULL)
        {
            if (ptex->glhandle != 0)
            {
                glDeleteTextures(1, &ptex->glhandle);
            }
            temptex = ptex;
            ptex = ptex->next;
            memFree(temptex);
        }

        texList = NULL;
    }

    if (btgHead != NULL)
    {
        numStars = btgHead->numStars;
        memFree(btgHead);
        btgHead = NULL;
    }
    else
    {
        numStars = 0;
    }
    if (btgVerts != NULL)
    {
        memFree(btgVerts);
        btgVerts = NULL;
    }
    if (btgStars != NULL)
    {
        memFree(btgStars);
        btgStars = NULL;
    }
    if (btgPolys != NULL)
    {
        memFree(btgPolys);
        btgPolys = NULL;
    }
    if (btgTransVerts != NULL)
    {
        memFree(btgTransVerts);
        btgTransVerts = NULL;
    }
    if (btgTransStars != NULL)
    {
        memFree(btgTransStars);
        btgTransStars = NULL;
    }
    if (btgIndices != NULL)
    {
        memFree(btgIndices);
        btgIndices = NULL;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgShutdown
    Description : shut down the btg subsystem
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgShutdown()
{
    btgReset();
}

/*-----------------------------------------------------------------------------
    Name        : btgSetTheta/btgGetTheta/
    Description : set/get the x theta offset of the entire background
    Inputs      : theta - amount, in degrees, to offset
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgSetTheta(real32 theta)
{
    btgThetaOffset = -theta;
}
real32 btgGetTheta(void)
{
    return(-btgThetaOffset);
}

/*-----------------------------------------------------------------------------
    Name        : btgSetPhi/btgGetPhi
    Description : set/get the y phi offset of the entire background
    Inputs      : phi - amount, in degrees, to offset
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgSetPhi(real32 phi)
{
    btgPhiOffset = -phi;
}
real32 btgGetPhi(void)
{
    return(-btgPhiOffset);
}

/*-----------------------------------------------------------------------------
    Name        : btgGetTexture
    Description : creates a GL texture object with the contents of the given .TGA file
    Inputs      : filename - the name of the bitmap, in btg\bitmaps
                  thandle - place where the texture name goes
                  width, height - dimensions of the bitmap
    Outputs     : thandle, width, height are modified
    Return      :
----------------------------------------------------------------------------*/
void btgGetTexture(char* filename, udword* thandle, sdword* width, sdword* height)
{
    char   fullname[64];
    ubyte* data;
    ubyte* pdata;
    ubyte* bp;
    unsigned short* psdata;
    TGAFileHeader head;
    sdword i;

#ifdef _WIN32
    #ifdef _WIN32_FIXME
      strcpy(fullname, "btg\\bitmaps\\b01.tga");
    #else
      strcpy(fullname, "btg\\bitmaps\\");
    #endif
#else
    strcpy(fullname, "btg/bitmaps/");
#endif
    strcat(fullname, filename);

    if (fileExists(fullname, 0))
    {
        fileLoadAlloc(fullname, (void**)&data, 0);

        //this pointer wackiness gets around alignment strangeness
        pdata = data;
        head.idLength = *pdata++;
        head.colorMapType = *pdata++;
        head.imageType = *pdata++;
        psdata = (unsigned short*)pdata;

#if FIX_ENDIAN
        head.colorMapStartIndex = FIX_ENDIAN_INT_16( *psdata );
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.colorMapNumEntries = FIX_ENDIAN_INT_16( *psdata );
        pdata += 2;
        head.colorMapBitsPerEntry = *pdata++;
        psdata = (unsigned short*)pdata;
        head.imageOffsetX = (signed short)FIX_ENDIAN_INT_16( *psdata );
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageOffsetY = (signed short)FIX_ENDIAN_INT_16( *psdata );
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageWidth = FIX_ENDIAN_INT_16( *psdata );
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageHeight = FIX_ENDIAN_INT_16( *psdata );
#else
        head.colorMapStartIndex = *psdata;
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.colorMapNumEntries = *psdata;
        pdata += 2;
        head.colorMapBitsPerEntry = *pdata++;
        psdata = (unsigned short*)pdata;
        head.imageOffsetX = (signed short)*psdata;
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageOffsetY = (signed short)*psdata;
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageWidth = *psdata;
        pdata += 2;
        psdata = (unsigned short*)pdata;
        head.imageHeight = *psdata;
#endif // FIX_ENDIAN

        pdata += 2;
        head.pixelDepth = *pdata++;
        head.imageDescriptor = *pdata++;

        //only 32bit TGAs
        dbgAssertOrIgnore(head.pixelDepth == 32);

        pdata += head.idLength;

        *width  = (sdword)head.imageWidth;
        *height = (sdword)head.imageHeight;

        //convert to GL_RGBA
        for (i = 0, bp = pdata; i < 4*(*width)*(*height); i += 4, bp += 4)
        {
            ubyte r, b;
            r = bp[0];
            b = bp[2];
            bp[0] = b;
            bp[2] = r;
            bp[3] = (ubyte)(255 - (int)bp[3]);
        }

        //create the GL texture object
        glGenTextures(1, thandle);
        glBindTexture(GL_TEXTURE_2D, *thandle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pdata);

        memFree(data);
    }
    else
    {
        *thandle = 0;
        *width   = 0;
        *height  = 0;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgTexInList
    Description : determines whether a given bitmap has already been loaded
    Inputs      : filename - the name of the bitmap
    Outputs     :
    Return      : TRUE or FALSE (in or not in list)
----------------------------------------------------------------------------*/
bool btgTexInList(char* filename)
{
    starTex* ptex;

    ptex = texList;
    while (ptex != NULL)
    {
        if (!strcmp(filename, ptex->filename))
        {
            return TRUE;
        }
        ptex = ptex->next;
    }

    return FALSE;
}

/*-----------------------------------------------------------------------------
    Name        : btgFindTexture
    Description : searches the star bitmap list to find a GL texture object
    Inputs      : filename - name of the bitmap
                  star - btg star structure to fill in
    Outputs     : star is modified accordingly
    Return      :
----------------------------------------------------------------------------*/
void btgFindTexture(char* filename, btgStar* star)
{
    starTex* ptex;

    ptex = texList;
    while (ptex != NULL)
    {
        if (!strcmp(filename, ptex->filename))
        {
            star->glhandle = ptex->glhandle;
            star->width = ptex->width;
            star->height = ptex->height;
            return;
        }
        ptex = ptex->next;
    }

    star->glhandle = 0;
    star->width = 0;
    star->height = 0;
}

/*-----------------------------------------------------------------------------
    Name        : btgAddTexToList
    Description : adds a starTex struct for a supplied gl texture name
    Inputs      : filename - name of the bitmap
                  glhandle - handle of the GL's texture object
                  width, height - dimensions of the bitmap
    Outputs     : texList is readjusted to contain a newly allocated starTex struct
    Return      :
----------------------------------------------------------------------------*/
void btgAddTexToList(char *filename, udword glhandle, sdword width, sdword height)
{
    starTex* newtex;

    newtex = (starTex*)memAlloc(sizeof(starTex), "btg star bitmap", 0);
    newtex->glhandle = glhandle;
    newtex->width = width;
    newtex->height = height;
    strcpy(newtex->filename, filename);
    newtex->next = NULL;

    if (texList == NULL)
    {
        //first bitmap loaded
        texList = newtex;
    }
    else
    {
        //other bitmaps exist
        newtex->next = texList;
        texList = newtex;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgCloseTextures
    Description : free texture handles
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgCloseTextures(void)
{
    sdword i;
    btgStar* pStar;

    if (btgStars != NULL)
    {
        for (i = 0, pStar = btgStars; i < btgHead->numStars; i++, pStar++)
        {
            if (pStar->glhandle != 0)
            {
                glDeleteTextures(1, &pStar->glhandle);
            }
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgLoadTextures
    Description : reload the background
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgLoadTextures(void)
{
    if (btgLastBackground[0] != '\0')
    {
        btgLoad(btgLastBackground);
        btgFade = -1;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgLoad
    Description : load a btg scene into our structures
    Inputs      : filename - the name of the btg file to load
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgLoad(char* filename)
{
    ubyte* btgData       = NULL;
    ubyte* btgDataOffset = NULL;
    ubyte* instarp       = NULL;
    udword headSize;
    udword vertSize;
    udword vertSizeFile;
    udword starSize, fileStarSize;
    udword polySize;
    real32 thetaSave, phiSave;
    sdword i;
#if FIX_ENDIAN
    Uint64 *swap;
#endif

#if BTG_VERBOSE_LEVEL >= 2
    dbgMessagef("filename= %s", filename);
#endif


    fileLoadAlloc(filename, (void**)&btgData, 0);
    btgDataOffset=btgData;

    memStrncpy(btgLastBackground, filename, 127);

    //reset / free any previous structures
    thetaSave = btgThetaOffset;
    phiSave = btgPhiOffset;
    btgReset();
    btgThetaOffset = thetaSave;
    btgPhiOffset = phiSave;

    //header.  trivial copy
    headSize = sizeof(btgHeader);
    btgHead = (btgHeader*)memAlloc(headSize, "btg header", 0);

#if BTG_VERBOSE_LEVEL >=3
 dbgMessagef("btgData= %x", btgData);
 dbgMessagef("btgHead= %x", btgHead);
#endif


// Hard coding sizeof values. 
// This is because they may change later on in the world but static in the file.
// This allows us to align variables. It replaces 
//  memcpy(btgHead, btgData, headSize);

    memset(btgHead,0,sizeof(btgHead));

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,btgFileVersion), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,numVerts      ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,numStars      ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,numPolys      ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,xScroll       ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,yScroll       ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,zoomVal       ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,pageWidth     ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,pageHeight    ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mRed          ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mGreen        ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mBlue         ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mBGRed        ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mBGGreen      ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,mBGBlue       ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,bVerts        ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,bPolys        ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,bStars        ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,bOutlines     ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,bBlends       ), btgDataOffset, 4 );
    btgDataOffset += 4;

    memcpy( (ubyte*)btgHead+offsetof(btgHeader,renderMode    ), btgDataOffset, 4 );
    btgDataOffset += 4;
        
//    memcpy(btgHead, btgData, headSize);  //See above.

#if FIX_ENDIAN
	btgHead->btgFileVersion = FIX_ENDIAN_INT_32( btgHead->btgFileVersion );
	btgHead->numVerts       = FIX_ENDIAN_INT_32( btgHead->numVerts );
	btgHead->numStars       = FIX_ENDIAN_INT_32( btgHead->numStars );
	btgHead->numPolys       = FIX_ENDIAN_INT_32( btgHead->numPolys );
	btgHead->xScroll        = FIX_ENDIAN_INT_32( btgHead->xScroll );
	btgHead->yScroll        = FIX_ENDIAN_INT_32( btgHead->yScroll );
	btgHead->zoomVal        = FIX_ENDIAN_INT_32( btgHead->zoomVal );
	btgHead->pageWidth      = FIX_ENDIAN_INT_32( btgHead->pageWidth );
	btgHead->pageHeight     = FIX_ENDIAN_INT_32( btgHead->pageHeight );
	btgHead->mRed           = FIX_ENDIAN_INT_32( btgHead->mRed );
	btgHead->mGreen         = FIX_ENDIAN_INT_32( btgHead->mGreen );
	btgHead->mBlue          = FIX_ENDIAN_INT_32( btgHead->mBlue );
	btgHead->mBGRed         = FIX_ENDIAN_INT_32( btgHead->mBGRed );
	btgHead->mBGGreen       = FIX_ENDIAN_INT_32( btgHead->mBGGreen );
	btgHead->mBGBlue        = FIX_ENDIAN_INT_32( btgHead->mBGBlue );
	btgHead->bVerts         = FIX_ENDIAN_INT_32( btgHead->bVerts );
	btgHead->bPolys         = FIX_ENDIAN_INT_32( btgHead->bPolys );
	btgHead->bStars         = FIX_ENDIAN_INT_32( btgHead->bStars );
	btgHead->bOutlines      = FIX_ENDIAN_INT_32( btgHead->bOutlines );
	btgHead->bBlends        = FIX_ENDIAN_INT_32( btgHead->bBlends );
	btgHead->renderMode     = FIX_ENDIAN_INT_32( btgHead->renderMode );
#endif

    //set background colour
    universe.backgroundColor = colRGB(btgHead->mBGRed, btgHead->mBGGreen, btgHead->mBGBlue);

    //version check
    dbgAssertOrIgnore(btgHead->btgFileVersion == BTG_FILE_VERSION);

    vertSize = btgHead->numVerts * sizeof(btgVertex);  //Machine Specific size

    vertSizeFile = btgHead->numVerts * (4 + (2* 8) + (5*4));  //Actual size from file. (No Alignment)

    if (vertSize)
    {

        btgVerts = (btgVertex*)memAlloc(vertSize, "btg verts", 0);

        for( i=0; i<btgHead->numVerts; i++ )
        {
            memcpy( (ubyte*)btgVerts+ ( i * sizeof(btgVertex)) +offsetof(btgVertex,flags     ), btgDataOffset, 4 );
            btgDataOffset += 4;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,x          ), btgDataOffset, 8 );
            btgDataOffset += 8;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,y          ), btgDataOffset, 8 );
            btgDataOffset += 8;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,red        ), btgDataOffset, 4 );
            btgDataOffset += 4;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,green      ), btgDataOffset, 4 );
            btgDataOffset += 4;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,blue       ), btgDataOffset, 4 );
            btgDataOffset += 4;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,alpha      ), btgDataOffset, 4 );
            btgDataOffset += 4;

            memcpy( (ubyte*)btgVerts+( i * sizeof(btgVertex)) +offsetof(btgVertex,brightness ), btgDataOffset, 4 );
            btgDataOffset += 4;
        }

//        memcpy(btgVerts, btgData + headSize, vertSize);  //Replaced by above.

#if FIX_ENDIAN
        for( i=0; i<btgHead->numVerts; i++ )
        {
            btgVerts[i].flags = FIX_ENDIAN_INT_32( btgVerts[i].flags );
            
            swap  = ( Uint64 *)&btgVerts[i].x;
            *swap = SDL_SwapLE64( *swap );
            
            swap  = ( Uint64 *)&btgVerts[i].y;
            *swap = SDL_SwapLE64( *swap );
            
            btgVerts[i].red        = FIX_ENDIAN_INT_32( btgVerts[i].red );
            btgVerts[i].green      = FIX_ENDIAN_INT_32( btgVerts[i].green );
            btgVerts[i].blue       = FIX_ENDIAN_INT_32( btgVerts[i].blue );
            btgVerts[i].alpha      = FIX_ENDIAN_INT_32( btgVerts[i].alpha );
            btgVerts[i].brightness = FIX_ENDIAN_INT_32( btgVerts[i].brightness );
        }
#endif
    }

#if BTG_VERBOSE_LEVEL >= 2
 dbgMessagef("numStars= %d", btgHead->numStars);
#endif

    //stars.  non-trivial munging around
    starSize = btgHead->numStars * sizeof(btgStar);
    fileStarSize = 0;
    if (starSize != 0)
    {
        btgStar* outstarp;
        btgStar* inp;
        udword*  udp;
        sdword   j, tempSize, count, length;
        char     filename[48];

        btgStars = (btgStar*)memAlloc(starSize, "btg stars", 0);
        instarp  = btgDataOffset;
#if BTG_VERBOSE_LEVEL >= 3
 dbgMessagef("instarp= %x",instarp);
 dbgMessagef("Offset= %x",instarp - btgData);
#endif
        outstarp = btgStars;
        inp = ( btgStar *)instarp;

        for (i = 0; i < btgHead->numStars; i++, outstarp++)
        {
            //extract constant-sized header
//            tempSize = sizeof(udword) + 2*sizeof(real64) + 4*sizeof(sdword);
            tempSize = 4 + 2*8 + 4*4;
            memcpy( (ubyte*)outstarp+offsetof(btgStar,flags), instarp, 4);
            instarp += 4;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,x),     instarp, 8);
            instarp += 8;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,y),     instarp, 8);
            instarp += 8;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,red),   instarp, 4);
            instarp += 4;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,green), instarp, 4);
            instarp += 4;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,blue),  instarp, 4);
            instarp += 4;

            memcpy( (ubyte*)outstarp+offsetof(btgStar,alpha), instarp, 4);
            instarp += 4;

//            memcpy(outstarp, instarp, tempSize); //Replaced by Above.
#if BTG_VERBOSE_LEVEL >= 3
 dbgMessagef("tempSize= %x", tempSize);
 dbgMessagef("instarp= %x", instarp);
#endif
            fileStarSize += tempSize;

#if FIX_ENDIAN
            swap = ( Uint64 *)&outstarp->x;
            *swap = SDL_SwapLE64( *swap );
            swap = ( Uint64 *)&outstarp->y;
            *swap = SDL_SwapLE64( *swap );
            outstarp->flags = FIX_ENDIAN_INT_32( outstarp->flags );
            outstarp->red   = FIX_ENDIAN_INT_32( outstarp->red );
            outstarp->green = FIX_ENDIAN_INT_32( outstarp->green );
            outstarp->blue  = FIX_ENDIAN_INT_32( outstarp->blue );
            outstarp->alpha = FIX_ENDIAN_INT_32( outstarp->alpha );
#endif

            //extract variable-sized filename
            count = 0;
            memset(filename, 0, 48);
            udp = (udword*)instarp;

#if FIX_ENDIAN
            length = FIX_ENDIAN_INT_32( (sdword)*udp );
#else
            length = (sdword)*udp;
#endif

#if BTG_VERBOSE_LEVEL >=4
 dbgMessagef("instarp= %x", instarp);
 dbgMessagef("udp= %x", udp);
 dbgMessagef("length= %d", length);
 dbgMessagef("filename= %s", filename);
#endif

            instarp += 4;
            fileStarSize += 4;
            for (j = 0; j < length; j++)
            {
                filename[count++] = *instarp++;
                fileStarSize++;
            }
            memcpy(outstarp->filename, filename, 48);

            //create a GL texture object
            if (!btgTexInList(filename))
            {
                btgGetTexture(filename, &outstarp->glhandle, &outstarp->width, &outstarp->height);
                btgAddTexToList(filename, outstarp->glhandle, outstarp->width, outstarp->height);
            }
            else
            {
                btgFindTexture(filename, outstarp);
            }
        }
    }

    //reset the game's current texture, which now differs from the GL's
    trClearCurrent();

    btgPolygon* polyOut;

    //polys.  trivial copy
    polySize = btgHead->numPolys * sizeof(btgPolygon);
    if (polySize != 0)
    {
        btgPolys = (btgPolygon*)memAlloc(polySize, "btg polys", 0);

        polyOut= btgPolys;
// HERE FIX IT HERE
//        memcpy(btgPolys, btgData + headSize + vertSize + fileStarSize, polySize);

	for( i=0; i<btgHead->numPolys; i++, polyOut++ )
	{
            memcpy((ubyte*)polyOut+offsetof(btgPolygon,flags), instarp, 4);
            instarp += 4;

            memcpy((ubyte*)polyOut+offsetof(btgPolygon,v0), instarp, 4);
            instarp += 4;

            memcpy((ubyte*)polyOut+offsetof(btgPolygon,v1), instarp, 4);
            instarp += 4;

            memcpy((ubyte*)polyOut+offsetof(btgPolygon,v2), instarp, 4);
            instarp += 4;

	}
		
#if FIX_ENDIAN
		for( i=0; i<btgHead->numPolys; i++ )
		{
			btgPolys[i].flags = FIX_ENDIAN_INT_32( btgPolys[i].flags );
			btgPolys[i].v0    = FIX_ENDIAN_INT_32( btgPolys[i].v0 );
			btgPolys[i].v1    = FIX_ENDIAN_INT_32( btgPolys[i].v1 );
			btgPolys[i].v2    = FIX_ENDIAN_INT_32( btgPolys[i].v2 );
		}
#endif
    }

    memFree(btgData);

    btgIndices = (udword*)memAlloc(3 * btgHead->numPolys * sizeof(udword), "btg indices", NonVolatile);

#ifndef _WIN32_FIXME
    //spherically project things, blend colours, &c
    btgConvertVerts();
#endif
}

/*-----------------------------------------------------------------------------
    Name        : btgSortCompare
    Description : callback from qsort to sort btg vertices by y coordinate
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
int btgSortCompare(const void* p0, const void* p1)
{
    btgVertex* va;
    btgVertex* vb;

    va = btgVerts + *((sdword*)p0);
    vb = btgVerts + *((sdword*)p1);

    if (va->y > vb->y)
    {
        return 1;
    }
    else if (va->y < vb->y)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgSortByY
    Description : sorts a list of vertex indices by y coordinate
    Inputs      : listToSort - the list of indices to sort
                  n - number of elements
    Outputs     : listToSort is sorted
    Return      :
----------------------------------------------------------------------------*/
void btgSortByY(sdword* listToSort, sdword n)
{
    qsort(listToSort, n, sizeof(sdword), btgSortCompare);
}

/*-----------------------------------------------------------------------------
    Name        : btgZip
    Description : fixup seaming
    Inputs      :
    Outputs     : btgPolys are adjusted to eliminate seaming
    Return      :
----------------------------------------------------------------------------*/
void btgZip(void)
{
    sdword i, iPoly;
    sdword numLeft, numRight;
    sdword lefts[64], rights[64];
    btgVertex* vert;
    btgPolygon* poly;

    //make lists of left, right verts
    numLeft = numRight = 0;
#if BTG_VERBOSE_LEVEL >=3
 dbgMessagef("numVerts= %d", btgHead->numVerts);
#endif

    for (i = 0, vert = btgVerts; i < btgHead->numVerts; i++, vert++)
    {
        if (vert->x < 0.5)
        {
            lefts[numLeft++] = i;
        }
        else if (vert->x > (btgHead->pageWidth - 0.5))
        {
            rights[numRight++] = i;
        }
    }

#if BTG_VERBOSE_LEVEL >=3
 dbgMessagef("numLeft= %d, numRight= %d\n", numLeft, numRight);
#endif

    //continue only if approximately equal number of edge verts
    if (ABS(numLeft - numRight) > 1)
    {
        return;
    }

    //sort lists
    btgSortByY(lefts, numLeft);
    btgSortByY(rights, numRight);

    //minimum of edge verts
    if (numLeft < numRight)
    {
        numRight = numLeft;
    }
    else if (numRight < numLeft)
    {
        numLeft = numRight;
    }

    //remove right refs
    for (i = 0; i < numLeft; i++)
    {
        for (iPoly = 0, poly = btgPolys; iPoly < btgHead->numPolys; iPoly++, poly++)
        {
            if (poly->v0 == rights[i])
            {
                poly->v0 = lefts[i];
            }
            if (poly->v1 == rights[i])
            {
                poly->v1 = lefts[i];
            }
            if (poly->v2 == rights[i])
            {
                poly->v2 = lefts[i];
            }
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgConvertAVert
    Description : spherically unprojects a vert
    Inputs      : out - target output vector
                  x, y - 2D position
    Outputs     : out is modified
    Return      :
----------------------------------------------------------------------------*/
void btgConvertAVert(vector* out, real64 x, real64 y)
{
    real32 xFrac, yFrac;
    real32 theta, phi;
    real32 sinTheta, cosTheta;
    real32 sinPhi, cosPhi;
    real32 pageWidth, pageHeight;
    real32 radius;

    pageWidth  = (real32)btgHead->pageWidth;
    pageHeight = (real32)btgHead->pageHeight;

    x = ((real64)pageWidth - 1.0) - x;

    xFrac = (real32)x / pageWidth;
    yFrac = (real32)y / pageHeight;

    theta = 2.0f * M_PI_F * xFrac;
    phi   = 1.0f * M_PI_F * yFrac;

    theta += DEG_TO_RAD(btgThetaOffset) + DEG_TO_RAD(90.0f);
    phi += DEG_TO_RAD(btgPhiOffset);

    sinTheta = fsin(theta);
    cosTheta = fcos(theta);
    sinPhi = fsin(phi);
    cosPhi = fcos(phi);

    radius = CAMERA_CLIP_FAR - 500.0f;
    out->x = radius * cosTheta * sinPhi;
    out->y = radius * sinTheta * sinPhi;
    out->z = radius * cosPhi;
}

/*-----------------------------------------------------------------------------
    Name        : btgConvertVert
    Description : do appropriate things to spherically unproject a vert
    Inputs      : out - target output btgTransVert
                  nVert - index of 2D btg vertex
    Outputs     : out will contain the appropriate thing
    Return      :
----------------------------------------------------------------------------*/
void btgConvertVert(btgTransVertex* out, udword nVert)
{
    btgVertex* in;

    in = btgVerts + nVert;

    btgConvertAVert(&out->position, in->x, in->y);

    out->red   = (GLubyte)((in->red   * in->alpha) >> 8);
    out->green = (GLubyte)((in->green * in->alpha) >> 8);
    out->blue  = (GLubyte)((in->blue  * in->alpha) >> 8);
    out->alpha = 255;
}

/*-----------------------------------------------------------------------------
    Name        : btgConvertStar
    Description : do appropriate things to spherically unproject a star
    Inputs      : out - target output btgTransVert
                  nVert - index of 2D btg star
    Outputs     : out will contain the appropriate thing
    Return      :
----------------------------------------------------------------------------*/
void btgConvertStar(btgTransStar* out, udword nVert)
{
    btgStar* in;
    real64   x, y;
    real64   halfWidth, halfHeight;

    in = btgStars + nVert;

    halfWidth  = (real64)in->width / 2.0;
    halfHeight = (real64)in->height / 2.0;

    btgConvertAVert(&out->mid, in->x, in->y);

    x = in->x - halfWidth;
    y = in->y - halfHeight;
    btgConvertAVert(&out->ul, x, y);

    x = in->x - halfWidth;
    y = in->y + halfHeight;
    btgConvertAVert(&out->ll, x, y);

    x = in->x + halfWidth;
    y = in->y + halfHeight;
    btgConvertAVert(&out->lr, x, y);

    x = in->x + halfWidth;
    y = in->y - halfHeight;
    btgConvertAVert(&out->ur, x, y);
}

/*-----------------------------------------------------------------------------
    Name        : btgConvertVerts
    Description : transforms 2D BTG vertices into 3D
    Inputs      :
    Outputs     : btgTransVerts, btgTransStars are allocated and initialized
    Return      :
----------------------------------------------------------------------------*/
void btgConvertVerts()
{
    udword          nVert, nPoly, index;
    btgTransVertex* pTransVert;
    btgTransStar*   pTransStar;
    btgPolygon*     pPoly;

    btgZip();

    btgTransVerts = memAlloc(btgHead->numVerts * sizeof(btgTransVertex), "btg trans verts", NonVolatile);

    if (btgHead->numStars > 0)
    {
        btgTransStars = memAlloc(btgHead->numStars * sizeof(btgTransStar), "btg trans stars", NonVolatile);
    }
    else
    {
        btgTransStars = NULL;
    }

    for (nVert = 0, pTransVert = btgTransVerts;
         nVert < btgHead->numVerts;
         nVert++, pTransVert++)
    {
        btgConvertVert(pTransVert, nVert);
    }

    for (nVert = 0, pTransStar = btgTransStars; nVert < btgHead->numStars; nVert++, pTransStar++)
    {
        btgConvertStar(pTransStar, nVert);
    }

    for (nPoly = 0, index = 0, pPoly = btgPolys;
         nPoly < btgHead->numPolys;
         nPoly++, pPoly++, index += 3)
    {
        btgIndices[index + 0] = pPoly->v0;
        btgIndices[index + 1] = pPoly->v1;
        btgIndices[index + 2] = pPoly->v2;
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgBlendColour
    Description : blend a colour to the current background colour
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgBlendColour(
    GLubyte* dr, GLubyte* dg, GLubyte* db, sdword sr, sdword sg, sdword sb, sdword sa)
{
    *dr = (GLubyte)((sr * sa) >> 8);
    *dg = (GLubyte)((sg * sa) >> 8);
    *db = (GLubyte)((sb * sa) >> 8);
    if (sa < 252)
    {
        if (*dr < _bgByte[0]) *dr = _bgByte[0];
        if (*dg < _bgByte[1]) *dg = _bgByte[1];
        if (*db < _bgByte[2]) *db = _bgByte[2];
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgVertexColor
    Description : sets the GL's colour from a btg vertex
    Inputs      : nVert - index of vertex in the big array
                  fastBlends - TRUE if alpha blending is fast
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgVertexColor(udword nVert, bool fastBlends)
{
    if (fastBlends)
    {
        btgVertex* pVert;
        sdword alpha;

        pVert = btgVerts + nVert;
        alpha = (pVert->alpha * pVert->brightness) >> 8;
        glColor4ub((GLubyte)pVert->red, (GLubyte)pVert->green,
                   (GLubyte)pVert->blue, (GLubyte)alpha);
    }
    else
    {
        btgVertex* pVert;
        GLubyte r, g, b, a;

        pVert = btgVerts + nVert;
        a = (pVert->alpha * pVert->brightness) >> 8;
        btgBlendColour(&r, &g, &b, pVert->red, pVert->green, pVert->blue, a);
        glColor3ub(r, g, b);
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgColorVertices
    Description : apply colouring to the interleaved color+vertex array
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgColorVertices(bool fastBlends)
{
    btgVertex* pVert;
    btgTransVertex* transVert;
    sdword nVert;
    GLubyte r, g, b, a;

    for (nVert = 0, pVert = btgVerts, transVert = btgTransVerts;
         nVert < btgHead->numVerts;
         nVert++, pVert++, transVert++)
    {
        if (fastBlends)
        {
            a = (pVert->alpha * pVert->brightness * btgFade) >> 16;
            transVert->red = pVert->red;
            transVert->green = pVert->green;
            transVert->blue = pVert->blue;
            transVert->alpha = a;
        }
        else
        {
            a = (pVert->alpha * pVert->brightness * btgFade) >> 16;
            btgBlendColour(&r, &g, &b, pVert->red, pVert->green, pVert->blue, a);
            transVert->red = r;
            transVert->green = g;
            transVert->blue = b;
            transVert->alpha = 255;
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : btgSetColourMultiplier
    Description : set colour multiplier to allow fading of the entire background
    Inputs      : t - [0..1], the parameter of fade-ness, default 1.0
    Outputs     : btgFade global == t * 255
    Return      :
----------------------------------------------------------------------------*/
void btgSetColourMultiplier(real32 t)
{
//    dbgAssertOrIgnore(t >= 0.0f && t <= 1.0f);
    btgFade = (sdword)(t * 255.0f);
}

/*-----------------------------------------------------------------------------
    Name        : btgRender
    Description : renders the background.  assumes modelview already set
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void btgRender()
{
    udword nStar;
    sdword lightOn, index;
    GLboolean texOn, blendOn, depthOn;
    bool fastBlends, textureStars, compiledArrays;
    real32 modelview[16], projection[16];
    udword* dlast;
    udword* dnext;
    static sdword lastFade = 255;

#if MR_KEYBOARD_CHEATS
    extern bool gMosaic;
#endif

    if (btgHead == NULL)
    {
#ifdef _WIN32
        btgLoad("BTG\\default.btg");
#else
        btgLoad("BTG/default.btg");
#endif
    }

#if MR_KEYBOARD_CHEATS
    glShadeModel(gMosaic ? GL_FLAT : GL_SMOOTH);
#else
    glShadeModel(GL_SMOOTH);
#endif
    glGetFloatv(GL_COLOR_CLEAR_VALUE, _bgColor);
    for (index = 0; index < 4; index++)
    {
        _bgByte[index] = (GLubyte)(_bgColor[index] * 255.0f);
    }

    dnext = (udword*)_bgByte;
    dlast = (udword*)lastbg;

    fastBlends = FALSE;//glCapFastFeature(GL_BLEND);
    textureStars = (RGLtype == SWtype) ? FALSE : TRUE;

    depthOn = glIsEnabled(GL_DEPTH_TEST);
    lightOn = rndLightingEnable(FALSE);
    texOn = glIsEnabled(GL_TEXTURE_2D);
    blendOn = glIsEnabled(GL_BLEND);
    if (RGLtype == SWtype)
    {
        glDisable(GL_DEPTH_TEST);
    }
    else
    {
        if (glCapFeatureExists(RGL_BROKEN_MIXED_DEPTHTEST))
        {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    if (fastBlends)
    {
        glEnable(GL_BLEND);
        rndAdditiveBlends(FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    //polys
    if (btgFade != lastFade || *dnext != *dlast)
    {
        if (btgFade < 0)
        {
            btgFade = 255;
        }
#ifndef _WIN32_FIXME
        btgColorVertices(fastBlends);
#endif
        *dlast = *dnext;
        lastFade = btgFade;
    }

    if (glCapFeatureExists(GL_VERTEX_ARRAY) &&
        (RGLtype != GLtype))
    {
        //use DrawElements to render the bg polys
        glInterleavedArrays(GL_C4UB_V3F, 0, (void*)btgTransVerts);
        if (glCapFeatureExists(GL_COMPILED_ARRAYS_EXT))
        {
            compiledArrays = TRUE;
            glLockArraysEXT(0, btgHead->numVerts);
        }
        else
        {
            compiledArrays = FALSE;
        }

        glDrawElements(GL_TRIANGLES, 3 * btgHead->numPolys, GL_UNSIGNED_INT, btgIndices);
        if (compiledArrays)
        {
            glUnlockArraysEXT();
        }
    }
    else
    {
        //simulate DrawElements for buggy GLs
        glBegin(GL_TRIANGLES);

#if BTG_VERBOSE_LEVEL >= 4
 dbgMessagef("numPolys= %d",btgHead->numPolys);
#endif

        for (index = 0; index < (3*btgHead->numPolys); index++)
        {
            btgTransVertex* pVert = &btgTransVerts[btgIndices[index]];
#if BTG_VERBOSE_LEVEL >= 4
 dbgMessagef("pVert.Red= %d",pVert->red);
 dbgMessagef("pVert.green= %d",pVert->green);
 dbgMessagef("pVert.blue= %d",pVert->blue);
 dbgMessagef("pVert.alpha= %d\n",pVert->alpha);
#endif

#ifndef _WIN32_FIXME
			glColor4ub(pVert->red, pVert->green, pVert->blue, pVert->alpha);
			glVertex3fv((GLfloat*)&pVert->position);
#endif
				}
        glEnd();
    }

    //stars
    rndPerspectiveCorrection(FALSE);

    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    if (textureStars)
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        rgluOrtho2D(0.0f, (GLfloat)MAIN_WindowWidth, 0.0f, (GLfloat)MAIN_WindowHeight);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    rndAdditiveBlends(TRUE);
    glEnable(GL_BLEND);

    trClearCurrent();

    for (nStar = 0; nStar < btgHead->numStars; nStar++)
    {
        btgTransStar* pStar;

        pStar = &btgTransStars[nStar];
        glColor4ub((GLubyte)btgStars[nStar].red,
                   (GLubyte)btgStars[nStar].green,
                   (GLubyte)btgStars[nStar].blue,
                   (GLubyte)btgStars[nStar].alpha);

        if (btgStars[nStar].glhandle != 0)
        {
            vector pscreen;
            bool   billboard = TRUE;

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, btgStars[nStar].glhandle);
            rndTextureEnvironment(RTE_Modulate);

#ifndef _WIN32_FIXME
            if (billboard)
            {
                if (clipPointToScreenWithMatrices(&pStar->ul, &pscreen, modelview, projection, 0) ||
                    clipPointToScreenWithMatrices(&pStar->ll, &pscreen, modelview, projection, 0) ||
                    clipPointToScreenWithMatrices(&pStar->lr, &pscreen, modelview, projection, 0) ||
                    clipPointToScreenWithMatrices(&pStar->ur, &pscreen, modelview, projection, 0))
                {
                    sdword width, height;
					
                    width  = btgStars[nStar].width  >> 1;
                    height = btgStars[nStar].height >> 1;

                    clipPointToScreenWithMatrices(&pStar->mid, &pscreen, modelview, projection, 1);

                    if (textureStars)//this one part doesn't crash on WIN32, but enabling it, i notice no difference
                    {
                        glColor3ub((ubyte)((btgStars[nStar].red * btgFade) >> 8),
                                   (ubyte)((btgStars[nStar].green * btgFade) >> 8),
                                   (ubyte)((btgStars[nStar].blue * btgFade) >> 8));
                        glBegin(GL_QUADS);

                        glTexCoord2f(0.0f, 0.0f);
                        glVertex2f(pscreen.x - width, pscreen.y - height);

                        glTexCoord2f(0.0f, 1.0f);
                        glVertex2f(pscreen.x - width, pscreen.y + height);

                        glTexCoord2f(1.0f, 1.0f);
                        glVertex2f(pscreen.x + width, pscreen.y + height);

                        glTexCoord2f(1.0f, 0.0f);
                        glVertex2f(pscreen.x + width, pscreen.y - height);

                        glEnd();
                    }
                    else
                    {
                        glTexCoord2f(pscreen.x - width, pscreen.y - height);
                        glDrawPixels(btgStars[nStar].width, btgStars[nStar].height,
                                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    }
                }
            }
            else
            {
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f);
                glVertex3fv((GLfloat*)&pStar->ul);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3fv((GLfloat*)&pStar->ll);
                glTexCoord2f(1.0f, 0.0f);
                glVertex3fv((GLfloat*)&pStar->lr);
                glTexCoord2f(1.0f, 1.0f);
                glVertex3fv((GLfloat*)&pStar->ur);
                glEnd();
            }
#endif// _WIN32_FIXME
         }
    }

    if (textureStars)
    {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(projection);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(modelview);
    }
    glDisable(GL_BLEND);

    rndAdditiveBlends(FALSE);

    rndLightingEnable(lightOn);
    if (texOn) glEnable(GL_TEXTURE_2D);
    if (blendOn)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glEnable(GL_CULL_FACE);
    if (depthOn)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    if (glCapFeatureExists(RGL_BROKEN_MIXED_DEPTHTEST))
    {
        glDepthMask(GL_TRUE);
    }
}
