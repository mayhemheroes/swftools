/* avi2swf.cc
   Convert avi movie files into swf.
   As soon as there's an algorithm implemented for writing the
   data directly to disk, this file should maybe go to ../src.

   Part of the swftools package.
   
   Copyright (c) 2001 Matthias Kramm <kramm@quiss.org>

   This file is distributed under the GPL, see file COPYING for details */

#include <stdio.h>
#include <fcntl.h>
extern "C" {
#include "../rfxswf.h"
#include "../args.h"
}
#include "avifile.h"
#include "aviplay.h"

char * filename = 0;
char * outputfilename = "output.swf";
unsigned int lastframe = 0xffffffff;

int jpeg_quality = 40;

struct options_t options[] =
{
 {"v","verbose"},
 {"o","output"},
 {"e","end"},
 {"V","version"},
 {0,0}
};

int args_callback_option(char*name,char*val)
{
    if(!strcmp(name, "V")) {
        printf("avi2swf - part of %s %s\n", PACKAGE, VERSION);
        exit(0);
    } 
    else if(!strcmp(name, "o")) {
	outputfilename = val;
	return 1;
    }
    else if(!strcmp(name, "e")) {
	lastframe = atoi(val);
	return 1;
    }
}
int args_callback_longoption(char*name,char*val)
{
    return args_long2shortoption(options, name, val);
}
void args_callback_usage(char*name)
{    
    printf("\nUsage: %s file.swf\n", name);
    printf("\t-h , --help\t\t Print help and exit\n");
    printf("\t-o , --output=filename\t Specify output filename\n"); 
    printf("\t-e , --end=frame\t\t Last frame to encode\n");
    printf("\t-V , --version\t\t Print program version and exit\n");
    exit(0);
}
int args_callback_command(char*name,char*val)
{
    if(filename) {
        fprintf(stderr, "Only one file allowed. You supplied at least two. (%s and %s)\n",
                 filename, name);
    }
    filename = name;
    return 0;
}

SWF swf;
TAG*tag;

int main (int argc,char ** argv)
{ int file;
  IAviReadFile* player;
  IAviReadStream* astream;
  IAviReadStream* vstream;
  MainAVIHeader head;
  SRECT r;

  processargs(argc, argv);
  if(!filename)
      exit(0);

  player = CreateIAviReadFile(filename);    
  player->GetFileHeader(&head);
  printf("fps: %d\n", 1000000/head.dwMicroSecPerFrame);
  printf("frames: %d\n", head.dwTotalFrames);
  printf("streams: %d\n", head.dwStreams);
  printf("streams: %d\n", player->StreamCount());
  printf("width: %d\n", head.dwWidth);
  printf("height: %d\n", head.dwHeight);
  
  astream = player->GetStream(0, AviStream::Audio);
  vstream = player->GetStream(0, AviStream::Video);

  vstream -> StartStreaming();
  
  file = open(outputfilename,O_WRONLY|O_CREAT|O_TRUNC, 0644);
  
  memset(&swf, 0, sizeof(swf));
  swf.frameRate = (int)(1000000.0/head.dwMicroSecPerFrame*256);
  swf.fileVersion = 4;
  swf.fileSize = 0x0fffffff;
  r.xmin = 0;
  r.ymin = 0;
  r.xmax = head.dwWidth*20;
  r.ymax = head.dwHeight*20;
  swf.movieSize = r;

  swf_WriteHeader(file, &swf);

  tag = swf_InsertTag(NULL, ST_SETBACKGROUNDCOLOR);
  swf_SetU8(tag,0); //black
  swf_SetU8(tag,0);
  swf_SetU8(tag,0);
  swf_WriteTag(file, tag);
  swf_DeleteTag(tag);

  U8*newdata = (U8*)malloc((head.dwWidth+3) * head.dwHeight * 4);

  int frame = 0;

  int lastsize = (head.dwWidth+3) * head.dwHeight * 4;
  U8* lastdata = (U8*)malloc(lastsize);
  U8* data;
  memset(lastdata,0, lastsize);

  while(1) {
    if(vstream->ReadFrame()<0) {
	printf("\n");
	break;
    }
    printf("\rconvert frame %d",frame);
    fflush(stdout);
    CImage*img = vstream->GetFrame();
    img->ToRGB();
    data = img->data();
    int width = img->width();
    int bpp = img->bpp();
    int width4 = width*4;
    int height = img->height();
    int x,y;
    int fs,ls;
    SHAPE*s;
    MATRIX m;
    SRECT r;
    RGBA rgb;

    if(frame!=0) {
	tag = swf_InsertTag(NULL, ST_REMOVEOBJECT2);
	swf_SetU16(tag, 1); //depth
	swf_WriteTag(file, tag);
	swf_DeleteTag(tag);
    }

    /* todo: dynamically decide whether to generate jpeg/lossless
       bitmaps, (using transparency to modify the previous 
       picture), and which jpeg compression depth to use.
       (btw: Are there video frame transitions which can
        reasonably be approximated by shapes?)
     */

    int type = 1;
    int rel = 0;
    if(type == 0) {
	tag = swf_InsertTag(NULL, ST_DEFINEBITSLOSSLESS);
	swf_SetU16(tag, frame*2);
	U8*mylastdata = lastdata;
	for(y=0;y<height;y++) {
	    U8*nd = &newdata[width4*y];
	    U8*mydata = img->at(y);
	    if(!rel)
	    for(x=0;x<width;x++) {
		nd[3]=mydata[0];
		nd[2]=mydata[1];
		nd[1]=mydata[2];
		mylastdata[2] = mydata[2];
		mylastdata[1] = mydata[1];
		mylastdata[0] = mydata[0];
		nd+=4;
		mydata+=3;
		mylastdata+=3;
	    }
	    else
	    for(x=0;x<width;x++) {
		int a = mylastdata[3]-data[3];
		int b = mylastdata[2]-data[2];
		int c = mylastdata[1]-data[1];
		if((a*a+b*b+c*c)>64)
		{
		    nd[3]=mydata[0];
		    nd[2]=mydata[1];
		    nd[1]=mydata[2];
		    nd[0]=255;
		} else {
		    nd[3]=0;
		    nd[2]=0;
		    nd[1]=0;
		    nd[0]=0;
		}
		mylastdata[2] = mydata[2];
		mylastdata[1] = mydata[1];
		mylastdata[0] = mydata[0];
		nd+=4;
		mydata+=3;
		mylastdata+=3;
	    }
	}
	swf_SetLosslessBits(tag,width,height,newdata,BMF_32BIT);
	swf_WriteTag(file, tag);
	swf_DeleteTag(tag);
    } else 
    if(type == 1) {
	tag = swf_InsertTag(NULL, ST_DEFINEBITSJPEG2);
	swf_SetU16(tag, frame*2);
	JPEGBITS * jb = swf_SetJPEGBitsStart(tag,width,height,jpeg_quality);
	U8*mylastdata = lastdata;
	for(y=0;y<height;y++) {
	    U8*nd = newdata;
	    U8*mydata = img->at(y);
	    for(x=0;x<width;x++) {
		nd[0] = mydata[2];
		nd[1] = mydata[1];
		nd[2] = mydata[0];
		if(rel) {
		    nd[0] = (mydata[2]-mylastdata[2])/2+0x80;
		    nd[1] = (mydata[1]-mylastdata[1])/2+0x80;
		    nd[2] = (mydata[0]-mylastdata[0])/2+0x80;
		}
		mylastdata[2] = mydata[2];
		mylastdata[1] = mydata[1];
		mylastdata[0] = mydata[0];
		nd+=3;
		mydata+=3;
		mylastdata+=3;
	    }
	    swf_SetJPEGBitsLine(jb,newdata);
	}
	swf_SetJPEGBitsFinish(jb);
	swf_WriteTag(file, tag);
	swf_DeleteTag(tag);
    }

    tag = swf_InsertTag(NULL, ST_DEFINESHAPE);
      swf_ShapeNew(&s);
      rgb.b = rgb.g = rgb.r = 0xff;
      ls = swf_ShapeAddLineStyle(s,20,&rgb);  
      swf_GetMatrix(NULL,&m);
      m.sx = 20*65536;
      m.sy = 20*65536;

      fs = swf_ShapeAddBitmapFillStyle(s,&m,frame*2,0);
      swf_SetU16(tag ,frame*2+1);   // ID   
      r.xmin = 0;
      r.ymin = 0;
      r.xmax = width*20;
      r.ymax = height*20;
      swf_SetRect(tag,&r);

      swf_SetShapeStyles(tag,s);
      swf_ShapeCountBits(s,NULL,NULL);
      swf_SetShapeBits(tag,s);

      swf_ShapeSetAll(tag,s,0,0,ls,fs,0);

      swf_ShapeSetLine(tag,s,width*20,0);
      swf_ShapeSetLine(tag,s,0,height*20);
      swf_ShapeSetLine(tag,s,-width*20,0);
      swf_ShapeSetLine(tag,s,0,-height*20);
      swf_ShapeSetEnd(tag);
    swf_WriteTag(file, tag);
    swf_DeleteTag(tag);

    tag = swf_InsertTag(NULL,ST_PLACEOBJECT2);
      swf_ObjectPlace(tag,frame*2+1,1,0,0,0);
    swf_WriteTag(file, tag);
    swf_DeleteTag(tag);

    tag = swf_InsertTag(NULL, ST_SHOWFRAME);
    swf_WriteTag(file, tag);
    swf_DeleteTag(tag);

    frame++;
    if(frame == lastframe)
	break;
  }
  free(newdata);
  printf("\n");
  
  tag = swf_InsertTag(NULL, ST_END);
  swf_WriteTag(file, tag);
  swf_DeleteTag(tag);

  close(file);

  return 0;
}

