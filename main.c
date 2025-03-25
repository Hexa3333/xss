/*
    Author: Alp Yilmaz
    License: MIT License

    Dependencies: xclip

    TODO:
	* Naming files
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <png.h>

#define MOUSE_LEFT  1UL
#define MOUSE_RIGHT 3UL

#define RECT_COLOR 0x00FF0000

bool flag_CopyToClipboard = false;
bool flag_CmdSpecifiedDimensions = false;
bool flag_OutputSpecified = false;
char* OutputFileName = NULL;

int SaveXImageAsPNG(XImage* img, const char* filePath);

int selectionTopLX = 0;
int selectionTopLY = 0;
int selectionWidth = 0;
int selectionHeight = 0;

int main(int argc, char** argv)
{
    Display* display = XOpenDisplay(NULL);
    if (!display)
    {
	fprintf(stderr, "Could not open display");
	return -1;
    }

    unsigned int scrWidth = DisplayWidth(display, 0);
    unsigned int scrHeight = DisplayHeight(display, 0);

    char c = 0;
    while ((c = getopt(argc, argv, "cp:o:")) != -1)
    {
	switch (c)
	{
	case 'c':
	{
	    flag_CopyToClipboard = true;
	    break;
	}
	case 'o':
	{
	    flag_OutputSpecified = true;
	    OutputFileName = optarg;
	    break;
	}
	case 'p':
	{
	    // Format: TopLX,TopLY,width,height
	    flag_CmdSpecifiedDimensions = true;
	    char topLXBuf[16];
	    char topLYBuf[16];
	    char widthBuf[16];
	    char heightBuf[16];

	    char* saveptr;
	    sprintf(topLXBuf, "%s", strtok_r(optarg, ",", &saveptr));
	    sprintf(topLYBuf, "%s", strtok_r(NULL, ",", &saveptr));
	    sprintf(widthBuf, "%s", strtok_r(NULL, ",", &saveptr));
	    sprintf(heightBuf, "%s", saveptr);

	    selectionTopLX = atoi(topLXBuf);
	    selectionTopLY = atoi(topLYBuf);
	    selectionWidth = atoi(widthBuf);
	    selectionHeight = atoi(heightBuf);

	    assert("Top left X position is invalid" &&
		    selectionTopLX > 0 && selectionTopLX < scrWidth);
	    assert("Top left Y position is invalid" &&
		    selectionTopLY > 0 && selectionTopLY < scrHeight);
	    assert("Selection width is invalid" && 
		    selectionWidth > 0 && selectionWidth + selectionTopLX < scrWidth);
	    assert("Selection height is invalid" &&
		    selectionHeight > 0 && selectionHeight + selectionTopLY < scrHeight);

	    XImage* subImg = XGetImage(display, DefaultRootWindow(display),
		    selectionTopLX, selectionTopLY,
		    selectionWidth, selectionHeight,
		    AllPlanes, ZPixmap);
	    SaveXImageAsPNG(subImg, "./");
	    
	    return 0;
	}
	case '?':
	{ return -1; }
	}
    }


    XImage* scrImg = XGetImage(display, DefaultRootWindow(display), 0, 0, scrWidth, scrHeight, AllPlanes, ZPixmap);

    Cursor cursor = XCreateFontCursor(display, XC_diamond_cross);
    /* override redirection of the window */
    XSetWindowAttributes winAttr;
    winAttr.override_redirect = True;
    winAttr.cursor = cursor;
    winAttr.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyReleaseMask | VisibilityChangeMask;
    Window window = XCreateWindow(display, DefaultRootWindow(display), 
	    0, 0, scrWidth, scrHeight, 0,
	    CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWCursor | CWEventMask, &winAttr);
    XMapRaised(display, window);

    Pixmap imgPixmap = XCreatePixmap(display, window, scrWidth, scrHeight, 24);
    XGCValues gcAttr;
    gcAttr.foreground = RECT_COLOR;
    GC gc = XCreateGC(display, window, GCForeground, &gcAttr);

    XPutImage(display, imgPixmap, gc, scrImg, 0, 0, 0, 0, scrWidth, scrHeight);
    XSetWindowBackgroundPixmap(display, window, imgPixmap);

    XClearWindow(display, window);


    bool selectionOn = false;
    bool leftSelection;
    int selectionOriginX = 0,
	selectionOriginY = 0;
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XEvent event = {0};
    while (1)
    {
	XNextEvent(display, &event);
	if (event.type == VisibilityPartiallyObscured ||
	    event.type == VisibilityFullyObscured)
	{
	    fprintf(stderr, "Visibility obscured - quitting...");
	    break;
	}

	/* Release ESC to let go of selection or quit */
	if (event.type == KeyRelease)
	{
	    KeySym keysym = XkbKeycodeToKeysym(display, event.xkey.keycode, 0, 0);
	    if (keysym == XK_Escape)
	    {
		if (selectionOn)
		{
		    selectionOn = false;
		    continue;
		}
		else
		    break;
	    }
	}
	else if (event.type == ButtonPress)
	{
	    if (!selectionOn)
	    {
		selectionOriginX = event.xbutton.x;
		selectionOriginY = event.xbutton.y;
	    }
	    selectionOn = true;

	    if (event.xbutton.button == MOUSE_LEFT)
		leftSelection = true;
	    else if (event.xbutton.button == MOUSE_RIGHT)
		leftSelection = false;
	}
	else if (event.type == ButtonRelease)
	{
	    if (selectionOn)
	    {
		// Time to process
		selectionOn = false;
		XImage* subImg = XGetImage(display, imgPixmap, selectionTopLX, selectionTopLY,
						     selectionWidth, selectionHeight,
						     AllPlanes, XYPixmap);
		SaveXImageAsPNG(subImg, "./");
		XDestroyImage(subImg);
		break;
	    }
	}
	else if (selectionOn && event.type == MotionNotify)
	{
	    XClearWindow(display, window);

	    selectionTopLX = (selectionOriginX < event.xbutton.x) ? selectionOriginX : event.xbutton.x;
	    selectionTopLY = (selectionOriginY < event.xbutton.y) ? selectionOriginY : event.xbutton.y;
	    selectionWidth = abs(event.xbutton.x - selectionOriginX);
	    selectionHeight = abs(event.xbutton.y - selectionOriginY);

	    XDrawRectangle(display, window, gc, selectionTopLX, selectionTopLY, selectionWidth, selectionHeight);
	}
    }


    XDestroyImage(scrImg);
    XFreeGC(display, gc);
    XFreePixmap(display, imgPixmap);

    XFreeCursor(display, cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

int SaveXImageAsPNG(XImage* img, const char* filePath)
{
    time_t timeNow = time(NULL);
    char* timestr = ctime(&timeNow);

    char fileName[128] = {0};
    strcpy(fileName, filePath);
    strcat(fileName, "/");
    strncat(fileName, timestr);
    strcat(fileName, ".png");


    FILE *fp = fopen(fileName, "wb");
    if (!fp)
    {
	perror("Image processing error!");
	return 0;
    }

    png_structp pngStructP = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)0, 0, 0);
    if (!pngStructP)
	return -1;

    png_infop pngInfoP = png_create_info_struct(pngStructP);
    if (!pngInfoP)
    {
	png_destroy_write_struct(&pngStructP, (png_infopp)NULL);
	return -1;
    }

    if (setjmp(png_jmpbuf(pngStructP)))
    {
	png_destroy_write_struct(&pngStructP, (png_infopp)NULL);
	return -1;
    }

    png_init_io(pngStructP, fp);
    png_set_IHDR(pngStructP, pngInfoP, img->width, img->height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(pngStructP, pngInfoP);

    png_bytep row = malloc(img->width * 3 * sizeof(png_byte));
    for (int y = 0; y < img->height; ++y)
    {
	for (int x = 0; x < img->width; ++x)
	{
	    unsigned long pixel = XGetPixel(img, x, y);
	    /* Assuming the pixel is in 0xRRGGBB format */
	    row[x*3 + 0] = (pixel >> 16) & 0xff;  /* Red */
	    row[x*3 + 1] = (pixel >> 8)  & 0xff;  /* Green */
	    row[x*3 + 2] = pixel & 0xff;          /* Blue */
	}
	png_write_row(pngStructP, row);
    }

    free(row);
    png_write_end(pngStructP, pngInfoP);
    png_destroy_write_struct(&pngStructP, &pngInfoP);
    fclose(fp);

    
    // Clipboard
    if (flag_CopyToClipboard)
    {
	int wstatus;
	pid_t xclip_pid = fork();
	if (xclip_pid == 0)
	{
	    execlp("xclip", "xclip",
		    "-selection", "clipboard",
		    "-t", "image/png",
		    "-i", fileName, NULL);
	    perror("xclip");
	}
	else if (xclip_pid == -1)
	{
	    perror("xclip");
	    return 0;
	}
	else
	{
	    wait(&wstatus);
	}
    }
}
