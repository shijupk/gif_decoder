// gif_decode.cpp : GIF Decoder
//

#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<string.h>


/////////////////// TYPE DEFENITIONS //////////////////////////////////////////

//typedef signed __int8     int8_t;
//typedef signed __int16    int16_t;
//typedef signed __int32    int32_t;
//typedef unsigned __int8   uint8_t;
//typedef unsigned __int16  uint16_t;
//typedef unsigned __int32  uint32_t;

typedef signed char       int8_t;
typedef signed short      int16_t;
typedef signed int        int32_t;
typedef unsigned char     uint8_t;
typedef unsigned short    uint16_t;
typedef unsigned int      uint32_t;

/* Various error codes used by decoder */
#define OUT_OF_MEMORY -10
#define BAD_CODE_SIZE -20
#define BAD_LINE_WIDTH -5
#define MAX_CODES   4095
#define GIFBUFTAM 16383
#define TRANSPARENCY_CODE 0xF9

//LZW GIF Image compression
#define HSIZE  5003     /* 80% occupancy */

///////////////////////////////////////////////////////////////////////////////


//////////////////////////////////// STRUCTURE DEFENITIONS ////////////////////
struct rgb_color { uint8_t r, g, b; };

typedef struct tagRGBQUAD {
    unsigned char    rgbBlue;
    unsigned char    rgbGreen;
    unsigned char    rgbRed;
    unsigned char    rgbReserved;
} RGBQUAD;


static const uint32_t code_mask[] = { 0x0000, 0x0001, 0x0003, 0x0007, 0x000F,
0x001F, 0x003F, 0x007F, 0x00FF,
0x01FF, 0x03FF, 0x07FF, 0x0FFF,
0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF };

#pragma pack(1)

typedef struct tag_gifgce {
    uint8_t flags; /*res:3|dispmeth:3|userinputflag:1|transpcolflag:1*/
    uint16_t delaytime;
    uint8_t transpcolindex;
} struct_gifgce;

typedef struct tag_dscgif {		/* Logic Screen Descriptor  */
    char header[6];				/* Firma and version */
    uint16_t scrwidth;
    uint16_t scrheight;
    char pflds;
    char bcindx;
    char pxasrat;
} struct_dscgif;

typedef struct tag_image {      /* Image Descriptor */
    uint16_t l;
    uint16_t t;
    uint16_t w;
    uint16_t h;
    uint8_t   pf;
} struct_image;

typedef struct tag_TabCol {
    int16_t colres;					/* color resolution */
    int16_t sogct;					/* size of global color table */
    rgb_color paleta[256];		/* paleta */
} struct_TabCol;

#pragma pack()

///////////////////////////////////////////////////////////////////////////////


/////////////// GLOBAL VARIABLES //////////////////////////////////////////////

int32_t interlaced;

int32_t ibf;
int32_t ibfmax;
uint8_t * buf;
uint32_t	dwFrameDelay;		///< used for GIF, MNG
                                /* The following static variables are used
                                * for seperating out codes */
int16_t navail_bytes;              /* # bytes left in block */
int16_t nbits_left;                /* # bits left in current uint8_t */
uint8_t b1;                           /* Current uint8_t */
uint8_t * byte_buff;               /* Current block */
uint8_t *pbytes;                      /* Pointer to next uint8_t in block */
                                      //uint8_t * buf;
uint8_t * stack;            /* Stack for storing pixels */
uint8_t * suffix;           /* Suffix table */
uint16_t * prefix;           /* Prefix linked list */


int32_t * htab;
uint16_t * codetab;
char * accum;

char * comment;
int32_t loops;

uint8_t	dispmeth;


int16_t curr_size;                     /* The current code size */
int16_t clear;                         /* Value for a clear code */
int16_t ending;                        /* Value for a ending code */
int16_t newcodes;                      /* First available code */
int16_t top_slot;                      /* Highest code for current size */
int16_t slot;                          /* Last read code */

int32_t	nBkgndIndex;		///< used for GIF, PNG, MNG
RGBQUAD nBkgndColor;		///< used for RGB transparency
int line_count = 0;

FILE *fp_read, *fp_write;

struct_gifgce gifgce;

///////////////////////////////////////////////////////////////////////////////


////////////////// FUNCTION DECLARATIONS //////////////////////////////////////

bool lets_open(FILE *fp);
int32_t get_frame(FILE *fp, struct_TabCol* TabColSrc, struct_dscgif* dscgif);
bool decode_extension(FILE *fp);
int16_t decoder(FILE *fp, bool to_buffer, int16_t linewidth, int32_t &bad_code_count);
int32_t get_byte(FILE *fp);
void init_gif();
int16_t init_exp(int16_t size);
int16_t get_next_code(FILE* fp);
void display_header_information(struct_dscgif dscgif);
int32_t out_line(uint8_t *pixels, int32_t linelen);

///////////////////////////////////////////////////////////////////////////////




int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        printf("Usage: %s < path to gif file> \n", argv[0]);
        exit(0);
    }

    fp_read = fopen(argv[1], "rb");

    if (fp_read == NULL)
    {
        printf("File not found! %s\n", argv[1]);
    }
    else
    {
        fp_write = fopen("decoded_file.bin", "ab");

        if (lets_open(fp_read))
        {
            printf("Decode completed successfuly.\n");
        }
        else
        {
            printf("Decode failed\n");
        }

        fclose(fp_write);
    }
    fclose(fp_read);

    getchar();

    return 0;

}

bool lets_open(FILE *fp)
{
    uint32_t pos = ftell(fp);

    init_gif();

    struct_dscgif dscgif;
    struct_TabCol TabCol;

    if (fp == NULL) return false;

    fread(&dscgif, /*sizeof(dscgif)*/13, 1, fp);

    display_header_information(dscgif);

    if (strncmp(dscgif.header, "GIF8", 4) != 0) return false;



    /* AD - for interlace */
    TabCol.sogct = (int16_t)(1 << ((dscgif.pflds & 0x07) + 1));
    TabCol.colres = (int16_t)(((dscgif.pflds & 0x70) >> 4) + 1);

    // assume that the image is a truecolor-gif if
    // 1) no global color map found
    // 2) (image.w, image.h) of the 1st image != (dscgif.scrwidth, dscgif.scrheight)
    int32_t bTrueColor = 0;


    if (dscgif.pflds & 0x80)
        fread(TabCol.paleta, sizeof(struct rgb_color)*TabCol.sogct, 1, fp);
    else
        bTrueColor++;	//first chance for a truecolor gif


    int32_t nNumFrames = get_frame(fp, &TabCol, &dscgif);

    printf("Num of frames: %d \n", nNumFrames);

    if (nNumFrames > 1)
    {
        printf("Number of frames is %d current version only support 1 frame", nNumFrames);
    }


    return true;


}


int32_t get_frame(FILE *fp, struct_TabCol* TabColSrc, struct_dscgif* dscgif)
{
    struct_image image;

    int32_t pos = ftell(fp);

    int32_t nframes = 0;

    struct_TabCol TempTabCol;
    memcpy(&TempTabCol, TabColSrc, sizeof(struct_TabCol));

    char ch;
    bool bPreviousWasNull = true;

    for (bool bContinue = true; bContinue; )
    {
        if (fread(&ch, sizeof(ch), 1, fp) != 1) { break; }

        if (bPreviousWasNull || ch == 0)
        {
            switch (ch)
            {
            case '!': // extension
            {
                decode_extension(fp);
                break;
            }
            case ',': // image
            {

                fread(&image, sizeof(image), 1, fp);


                // in case of images with empty screen descriptor, give a last chance
                if (dscgif->scrwidth == 0 && dscgif->scrheight == 0) {
                    dscgif->scrwidth = image.w;
                    dscgif->scrheight = image.h;
                }

                if (((image.l + image.w) > dscgif->scrwidth) || ((image.t + image.h) > dscgif->scrheight))
                    break;

                nframes++;

                // Local colour map?
                if (image.pf & 0x80) {
                    TempTabCol.sogct = (int16_t)(1 << ((image.pf & 0x07) + 1));
                    fread(TempTabCol.paleta, sizeof(struct rgb_color)*TempTabCol.sogct, 1, fp);
                }

                int32_t badcode = 0;
                ibf = GIFBUFTAM + 1;

                interlaced = image.pf & 0x40;

                int32_t pos_start = ftell(fp);

                decoder(fp, true, image.w, badcode);

                fseek(fp, -(ibfmax - ibf - 1), SEEK_CUR);

                break;
            }
            case ';': //terminator
                bContinue = false;
                break;
            default:
                bPreviousWasNull = (ch == 0);
                break;
            }
        }
    }
    fseek(fp, pos, SEEK_SET);

    return nframes;
}


bool decode_extension(FILE *fp)
{
    bool bContinue;
    uint8_t count;
    uint8_t fc;

    bContinue = (1 == fread(&fc, sizeof(fc), 1, fp));
    if (bContinue) {
        /* AD - for transparency */
        if (fc == 0xF9) {
            bContinue = (1 == fread(&count, sizeof(count), 1, fp));
            if (bContinue) {
                bContinue = (count == fread(&gifgce, 1, sizeof(gifgce), fp));
                if (bContinue) {
                    nBkgndIndex = (gifgce.flags & 0x1) ? gifgce.transpcolindex : -1;
                    dwFrameDelay = gifgce.delaytime;
                    dispmeth = ((gifgce.flags >> 2) & 0x7);
                }
            }
        }

        if (fc == 0xFE) { //<DP> Comment block
            bContinue = (1 == fread(&count, sizeof(count), 1, fp));
            if (bContinue) {
                bContinue = (1 == fread(comment, count, 1, fp));
                comment[count] = '\0';
            }
        }

        if (fc == 0xFF) { //<DP> Application Extension block
            bContinue = (1 == fread(&count, sizeof(count), 1, fp));
            if (bContinue) {
                bContinue = (count == 11);
                if (bContinue) {
                    char AppID[11];
                    bContinue = (1 == fread(AppID, count, 1, fp));
                    if (bContinue) {
                        bContinue = (1 == fread(&count, sizeof(count), 1, fp));
                        if (bContinue) {
                            uint8_t* dati = (uint8_t*)malloc(count);
                            bContinue = (dati != NULL);
                            if (bContinue) {
                                bContinue = (1 == fread(dati, count, 1, fp));
                                if (count > 2) {
                                    loops = dati[1] + 256 * dati[2];
                                }
                            }
                            free(dati);
                        }
                    }
                }
            }
        }

        while (bContinue && fread(&count, sizeof(count), 1, fp) && count) {
            printf("Skipping %d bytes", count);
            fseek(fp, count, SEEK_CUR);
        }
    }
    return bContinue;

}

void init_gif()
{
    buf = new uint8_t[GIFBUFTAM + 1];

    stack = new uint8_t[MAX_CODES + 1];
    suffix = new uint8_t[MAX_CODES + 1];
    prefix = new uint16_t[MAX_CODES + 1];

    htab = new int32_t[HSIZE];
    codetab = new uint16_t[HSIZE];

    byte_buff = new uint8_t[257];
    accum = new char[256];
    comment = new char[256];

    loops = 0;
    dispmeth = 0;
    comment[0] = '\0';
}


int16_t decoder(FILE *fp, bool to_buffer, int16_t linewidth, int32_t &bad_code_count)
{
    register uint8_t *sp, *bufptr;
    uint8_t *buf;
    register int16_t code, fc, oc, bufcnt;
    int16_t c, size, ret;

    if (linewidth <= 0)
        return BAD_LINE_WIDTH;

    /* Initialize for decoding a new image... */
    bad_code_count = 0;
    if ((size = (int16_t)get_byte(fp)) < 0)	return(size);
    if (size < 2 || 9 < size)				return(BAD_CODE_SIZE);


    init_exp(size);
    //printf("L %d %x\n",linewidth,size);

    /* Initialize in case they forgot to put in a clear code.
    * (This shouldn't happen, but we'll try and decode it anyway...)
    */
    oc = fc = 0;

    /* Allocate space for the decode buffer */
    if ((buf = new uint8_t[linewidth + 1]) == NULL) return(OUT_OF_MEMORY);

    /* Set up the stack pointer and decode buffer pointer */
    sp = stack;
    bufptr = buf;
    bufcnt = linewidth;

    /* This is the main loop.  For each code we get we pass through the
    * linked list of prefix codes, pushing the corresponding "character" for
    * each code onto the stack.  When the list reaches a single "character"
    * we push that on the stack too, and then start unstacking each
    * character for output in the correct order.  Special handling is
    * included for the clear code, and the whole thing ends when we get
    * an ending code.
    */
    while ((c = get_next_code(fp)) != ending) {
        /* If we had a file error, return without completing the decode*/
        if (c < 0) {
            delete[] buf;
            return(0);
        }
        /* If the code is a clear code, reinitialize all necessary items.*/
        if (c == clear) {
            curr_size = (int16_t)(size + 1);
            slot = newcodes;
            top_slot = (int16_t)(1 << curr_size);

            /* Continue reading codes until we get a non-clear code
            * (Another unlikely, but possible case...)
            */
            while ((c = get_next_code(fp)) == clear);

            /* If we get an ending code immediately after a clear code
            * (Yet another unlikely case), then break out of the loop.
            */
            if (c == ending) break;

            /* Finally, if the code is beyond the range of already set codes,
            * (This one had better NOT happen...  I have no idea what will
            * result from this, but I doubt it will look good...) then set it
            * to color zero.
            */
            if (c >= slot) c = 0;
            oc = fc = c;

            /* And let us not forget to put the char into the buffer... And
            * if, on the off chance, we were exactly one pixel from the end
            * of the line, we have to send the buffer to the out_line()
            * routine...
            */
            *bufptr++ = (uint8_t)c;
            if (--bufcnt == 0) {
                if (to_buffer) {
                    if ((ret = (int16_t)out_line(buf, linewidth)) < 0) {
                        delete[] buf;
                        return(ret);
                    }
                }

                bufptr = buf;
                bufcnt = linewidth;
            }
        }
        else {
            /* In this case, it's not a clear code or an ending code, so
            * it must be a code code...  So we can now decode the code into
            * a stack of character codes. (Clear as mud, right?)
            */
            code = c;

            /* Here we go again with one of those off chances...  If, on the
            * off chance, the code we got is beyond the range of those already
            * set up (Another thing which had better NOT happen...) we trick
            * the decoder into thinking it actually got the last code read.
            * (Hmmn... I'm not sure why this works...  But it does...)
            */
            if (code >= slot && sp < (stack + MAX_CODES - 1)) {
                if (code > slot)
                    ++bad_code_count;
                code = oc;
                *sp++ = (uint8_t)fc;
            }

            /* Here we scan back along the linked list of prefixes, pushing
            * helpless characters (ie. suffixes) onto the stack as we do so.
            */
            while (code >= newcodes && sp < (stack + MAX_CODES - 1)) {
                *sp++ = suffix[code];
                code = prefix[code];
            }

            /* Push the last character on the stack, and set up the new
            * prefix and suffix, and if the required slot number is greater
            * than that allowed by the current bit size, increase the bit
            * size.  (NOTE - If we are all full, we *don't* save the new
            * suffix and prefix...  I'm not certain if this is correct...
            * it might be more proper to overwrite the last code...
            */
            *sp++ = (uint8_t)code;
            if (slot < top_slot) {
                suffix[slot] = (uint8_t)(fc = (uint8_t)code);
                prefix[slot++] = oc;
                oc = c;
            }
            if (slot >= top_slot) {
                if (curr_size < 12) {
                    top_slot <<= 1;
                    ++curr_size;
                }
            }

            /* Now that we've pushed the decoded string (in reverse order)
            * onto the stack, lets pop it off and put it into our decode
            * buffer...  And when the decode buffer is full, write another
            * line...
            */
            while (sp > stack) {
                *bufptr++ = *(--sp);
                if (--bufcnt == 0) {
                    if (to_buffer)
                    {
                        if ((ret = (int16_t)out_line(buf, linewidth)) < 0) {
                            delete[] buf;
                            return(ret);
                        }
                    }
                    bufptr = buf;
                    bufcnt = linewidth;
                }
            }
        }
    }
    ret = 0;
    if (bufcnt != linewidth && to_buffer)
        ret = (int16_t)out_line(buf, (linewidth - bufcnt));
    delete[] buf;
    return(ret);
}

int32_t get_byte(FILE *fp)
{
    if (ibf >= GIFBUFTAM) {
        ibfmax = (int32_t)fread(buf, 1, GIFBUFTAM, fp);
        if (ibfmax < GIFBUFTAM) buf[ibfmax] = 255;

        ibf = 0;
    }
    if (ibf >= ibfmax) return -1; //<DP> avoid overflows
    return buf[ibf++];
}

int32_t out_line(uint8_t *pixels, int32_t linelen)
{
    if (pixels == NULL)
        return -1;

    //printf("count: %d pixel row: %d \n", line_count++, linelen);

    fwrite(pixels, 1, linelen, fp_write);
    return 0;
}


int16_t init_exp(int16_t size)
{
    curr_size = (int16_t)(size + 1);
    top_slot = (int16_t)(1 << curr_size);
    clear = (int16_t)(1 << size);
    ending = (int16_t)(clear + 1);
    slot = newcodes = (int16_t)(ending + 1);
    navail_bytes = nbits_left = 0;

    memset(stack, 0, MAX_CODES + 1);
    memset(prefix, 0, MAX_CODES + 1);
    memset(suffix, 0, MAX_CODES + 1);
    return(0);
}


/* get_next_code()
* - gets the next code from the GIF file.  Returns the code, or else
* a negative number in case of file errors...
*/
int16_t get_next_code(FILE* fp)
{
    int16_t i, x;
    uint32_t ret;

    if (nbits_left == 0) {
        if (navail_bytes <= 0) {
            /* Out of bytes in current block, so read next block */
            pbytes = byte_buff;
            if ((navail_bytes = (int16_t)get_byte(fp)) < 0)
                return(navail_bytes);
            else if (navail_bytes) {
                for (i = 0; i < navail_bytes; ++i) {
                    if ((x = (int16_t)get_byte(fp)) < 0) return(x);
                    byte_buff[i] = (uint8_t)x;
                }
            }
        }
        b1 = *pbytes++;
        nbits_left = 8;
        --navail_bytes;
    }

    if (navail_bytes < 0) return ending; // prevent deadlocks (thanks to Mike Melnikov)

    ret = b1 >> (8 - nbits_left);
    while (curr_size > nbits_left) {
        if (navail_bytes <= 0) {
            /* Out of bytes in current block, so read next block*/
            pbytes = byte_buff;
            if ((navail_bytes = (int16_t)get_byte(fp)) < 0)
                return(navail_bytes);
            else if (navail_bytes) {
                for (i = 0; i < navail_bytes; ++i) {
                    if ((x = (int16_t)get_byte(fp)) < 0) return(x);
                    byte_buff[i] = (uint8_t)x;
                }
            }
        }
        b1 = *pbytes++;
        ret |= b1 << nbits_left;
        nbits_left += 8;
        --navail_bytes;
    }
    nbits_left = (int16_t)(nbits_left - curr_size);
    ret &= code_mask[curr_size];
    return((int16_t)(ret));
}

/////////////////////// DISPLAY FUNCTIONS /////////////////////////////////////

void display_header_information(struct_dscgif dscgif)
{
    printf("GIF version is = ");
    printf("%c", dscgif.header[0]);
    printf("%c", dscgif.header[1]);
    printf("%c", dscgif.header[2]);
    printf("%c", dscgif.header[3]);
    printf("%c", dscgif.header[4]);
    printf("%c\n", dscgif.header[5]);
    printf("Pixel width is = %d\n", dscgif.scrwidth);
    printf("Pixel height is = %d\n", dscgif.scrheight);
    printf("Gif field = %d\n", dscgif.pflds);

}

///////////////////////////////////////////////////////////////////////////////