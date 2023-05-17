#include "thermapp.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <ctype.h>
#include <ncurses.h>

#include "fitsio.h"
#include <time.h>

#define BUF_LEN 256
#define NDARKS 11
#define DETNAM "ThermApp"

#undef FRAME_RAW

#ifndef FRAME_RAW
#define FRAME_FORMAT V4L2_PIX_FMT_YUV420
#else
#define FRAME_FORMAT V4L2_PIX_FMT_Y16
#endif

#define ROUND_UP_2(num) (((num)+1)&~1)
#define ROUND_UP_4(num) (((num)+3)&~3)
#define ROUND_UP_8(num)  (((num)+7)&~7)
#define ROUND_UP_16(num) (((num)+15)&~15)
#define ROUND_UP_32(num) (((num)+31)&~31)
#define ROUND_UP_64(num) (((num)+63)&~63)


//int write_fits_fname(int16_t *frame_arr, char *fname);
//int write_fits_fname(int16_t *frame_arr, char *fname, char *imgtyp);
int write_fits_fname(int16_t *frame_arr, char *fname, char *imgtyp, float TempC);
int get_science_fname(char *opfname);
int get_dark_fname(char *opfname, int framecount);
int format_properties(const unsigned int format,
                      const unsigned int width,
                      const unsigned int height,
                      size_t *framesize,
                      size_t *linewidth);
int main(int argc, char *argv[]);

int format_properties(const unsigned int format,
                      const unsigned int width,
                      const unsigned int height,
                      size_t *framesize,
                      size_t *linewidth)
{
	unsigned int lw, fs;
	switch (format) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		lw = width; /* ??? */
		fs = ROUND_UP_4(width) * ROUND_UP_2(height);
		fs += 2 * ((ROUND_UP_8(width) / 2) * (ROUND_UP_2(height) / 2));
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_Y41P:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		lw = (ROUND_UP_2(width) * 2);
		fs = lw * height;
		break;
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_Y16:
	case V4L2_PIX_FMT_Y16_BE:
		lw = 2 * width;
		fs = lw * height;
		break;
	default:
		return -1;
	}
	if (framesize) *framesize = fs;
	if (linewidth) *linewidth = lw;

	return 0;
}

int main(int argc, char *argv[])
{
	int16_t frame[PIXELS_DATA_SIZE];
	int ret = EXIT_SUCCESS;
	char fnam[BUF_LEN] = {0};
	float ThermTempC;
	const char *VIDEO_DEVICE = NULL;

	if (argc != 2) {
		printf("Usage: sudo astrotherm /dev/videoX\n");
		return 0;
	}

	VIDEO_DEVICE = argv[1];

	ThermApp *therm = thermapp_open();
	if (!therm) {
		ret = EXIT_FAILURE;
		goto done1;
	}

	// Discard 1st frame, it usually has the header repeated twice
	// and the data shifted into the pad by a corresponding amount.
	if (thermapp_usb_connect(therm)
	 || thermapp_thread_create(therm)
	 || thermapp_getImage(therm, frame)) {
		ret = EXIT_FAILURE;
		goto done2;
	}

	ThermTempC = thermapp_getTemperature(therm);
	printf("Serial number: %d\n", thermapp_getSerialNumber(therm));
	printf("Hardware version: %d\n", thermapp_getHardwareVersion(therm));
	printf("Firmware version: %d\n", thermapp_getFirmwareVersion(therm));
	printf("Temperature: %f\n", ThermTempC);

#ifndef FRAME_RAW
	int flipv = 0;
	if (argc >= 2) {
		flipv = *argv[1];
	}

	// get cal
	double pre_offset_cal = 0;
	double gain_cal = 1;
	double offset_cal = 0;
	long meancal = 0;
	int image_cal[PIXELS_DATA_SIZE];
	int deadpixel_map[PIXELS_DATA_SIZE] = { 0 };

	memset(image_cal, 0, sizeof image_cal);
	printf("Calibrating... cover the lens!\n");
	for (int i = 0; i < NDARKS; i++) {
		ThermTempC = thermapp_getTemperature(therm);
		if (thermapp_getImage(therm, frame)) {
			goto done2;
		}
		ret = get_dark_fname(fnam, i);
		ret = write_fits_fname(frame, fnam, "DARK", ThermTempC);

		printf("\rCaptured calibration frame %d/%d: %s\n", i+1,NDARKS,fnam);
		fflush(stdout);

		for (int j = 0; j < PIXELS_DATA_SIZE; j++) {
			image_cal[j] += frame[j];
		}
	}
	printf("\nCalibration finished\n");

	for (int i = 0; i < PIXELS_DATA_SIZE; i++) {
		image_cal[i] /= NDARKS;
		meancal += image_cal[i];
	}
	meancal /= PIXELS_DATA_SIZE;
	// record the dead pixels
	for (int i = 0; i < PIXELS_DATA_SIZE; i++) {
		if ((image_cal[i] > meancal + 250) || (image_cal[i] < meancal - 250)) {
			//printf("Dead pixel ID: %d (%d vs %li)\n", i, image_cal[i], meancal);
			deadpixel_map[i] = 1;
		}
	}
	// end of get cal
#endif

	struct v4l2_format vid_format;

	int fdwr = open(VIDEO_DEVICE, O_WRONLY);
	if (fdwr < 0) {
		perror("open");
		ret = EXIT_FAILURE;
		goto done2;
	}

	memset(&vid_format, 0, sizeof vid_format);
	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (ioctl(fdwr, VIDIOC_G_FMT, &vid_format)) {
		perror("VIDIOC_G_FMT");
		ret = EXIT_FAILURE;
		goto done3;
	}

	vid_format.fmt.pix.width = FRAME_WIDTH;
	vid_format.fmt.pix.height = FRAME_HEIGHT;
	vid_format.fmt.pix.pixelformat = FRAME_FORMAT;
	vid_format.fmt.pix.field = V4L2_FIELD_NONE;
	vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

	size_t framesize;
	size_t linewidth;
	if (format_properties(vid_format.fmt.pix.pixelformat,
	                      vid_format.fmt.pix.width, vid_format.fmt.pix.height,
	                      &framesize,
	                      &linewidth)) {
		fprintf(stderr, "unable to guess correct settings for format '%d'\n", FRAME_FORMAT);
		ret = EXIT_FAILURE;
		goto done3;
	}
	vid_format.fmt.pix.sizeimage = framesize;
	vid_format.fmt.pix.bytesperline = linewidth;

	if (ioctl(fdwr, VIDIOC_S_FMT, &vid_format)) {
		perror("VIDIOC_S_FMT");
		ret = EXIT_FAILURE;
		goto done3;
	}

	char ch;
	initscr();
	nodelay(stdscr, true);
	noecho();

	while (thermapp_getImage(therm, frame) == 0) {
#ifndef FRAME_RAW
		uint8_t img[PIXELS_DATA_SIZE * 3 / 2];
		int i;
		int frameMax = ((frame[0] + pre_offset_cal - image_cal[0]) * gain_cal) + offset_cal;
		int frameMin = ((frame[0] + pre_offset_cal - image_cal[0]) * gain_cal) + offset_cal;
		for (i = 0; i < PIXELS_DATA_SIZE; i++) { // get the min and max values
			// only bother if the pixel isn't dead
			if (!deadpixel_map[i]) {
				int x = ((frame[i] + pre_offset_cal - image_cal[i]) * gain_cal) + offset_cal;
				if (x > frameMax) {
					frameMax = x;
				}
				if (x < frameMin) {
					frameMin = x;
				}
			}
		}
		// second time through, this time actually scaling data
		for (i = 0; i < PIXELS_DATA_SIZE; i++) {
			int x = ((frame[i] + pre_offset_cal - image_cal[i]) * gain_cal) + offset_cal;
			if (deadpixel_map[i]) {
				x = ((frame[i-1] + pre_offset_cal - image_cal[i-1]) * gain_cal) + offset_cal;
			}
			x = (((double)x - frameMin)/(frameMax - frameMin)) * (235 - 16) + 16;
			if (flipv) {
				img[PIXELS_DATA_SIZE - ((i/FRAME_WIDTH)+1)*FRAME_WIDTH + i%FRAME_WIDTH] = x;
			} else {
				img[((i/FRAME_WIDTH)+1)*FRAME_WIDTH - i%FRAME_WIDTH - 1] = x;
			}
		}
		for (; i < sizeof img; i++) {
			img[i] = 128;
		}
		write(fdwr, img, sizeof img);
#else
		write(fdwr, frame, sizeof frame);
#endif
		ch = getch();
		if (toupper(ch) == 'S') {
		        ret = get_science_fname(fnam);
			ThermTempC = thermapp_getTemperature(therm);
			ret = write_fits_fname(frame, fnam, "SCIENCE", ThermTempC);
			fprintf(stdout,"Saved %s\n",fnam);
		}
		if (toupper(ch) == 'Q') {
			endwin();
			printf("User asked to quit.\n");
			//goto done3;
			close(fdwr);
			thermapp_close(therm);
			return ret;
		}
	}

done3:
	close(fdwr);
done2:
	thermapp_close(therm);
done1:
	return ret;
}



/* This function creates the output file name of a science frame based 
 * on the current UTC */
int get_science_fname(char *opfname)
{
    time_t now = time(&now);
    
    if (now == -1) {
        puts("The time() function failed");
    }
        
    struct tm *ptm = gmtime(&now);
    
    if (ptm == NULL) {
        puts("The gmtime() function failed");
    }    
    
    strftime(opfname, BUF_LEN, "thermapp_%Y%m%d_%H%M%S.fits", ptm);
    return 0;
}

/* This function creates the output file name of a dark frame based 
 * on the current UTC and frame counter */
int get_dark_fname(char *opfname, int framecount)
{
    char fc[BUF_LEN];
    time_t now = time(&now);
    
    if (now == -1) {
        puts("The time() function failed");
    }
        
    struct tm *ptm = gmtime(&now);
    
    if (ptm == NULL) {
        puts("The gmtime() function failed");
    }    
    
    strftime(opfname, BUF_LEN, "thermapp_%Y%m%d_%H%M", ptm);

    sprintf(fc,"_dark%02d.fits",framecount+1);
    strcat(opfname, fc);
    return 0;
}


int write_fits_fname(int16_t *frame_arr, char *fname, char *imgtyp, float TempC)
{
	int jj;
	int status = 0;        /* initialize status before calling fitsio  */
	int bitpix =  16;      /* 16-bit short signed integer pixel values */
	long fpixel = 1;                           /* first pixel to write */
	long array[PIXELS_DATA_SIZE];
	long naxis =   2;                           /* 2-dimensional image */
	long naxes[2] = {FRAME_WIDTH, FRAME_HEIGHT};

	fitsfile *fptr;                        /* pointer to the FITS file */
	
	if ( fits_create_file(&fptr, fname, &status) )      /* create FITS */
		return( status );
	
	/* Write the required keywords for the primary array image         */
	if ( fits_create_img(fptr,  bitpix, naxis, naxes, &status) )
		return( status );
	if ( fits_write_date(fptr, &status) )
		return( status );
	if ( fits_update_key(fptr, TSTRING, "INSTRUME", &DETNAM, 
				"Detector", &status) )
		return ( status );
	if ( fits_update_key(fptr, TSTRING, "IMGTYPE", imgtyp, 
				"Science or Dark image", &status) )
		return ( status );
	if ( fits_update_key(fptr, TFLOAT, "DET_TEMP", &TempC, 
				"Temperature [C]", &status) )
		return ( status );

	/* Convert int16_t to long */
	for (jj = 0; jj < PIXELS_DATA_SIZE; jj++)
		array[jj] = (long)frame_arr[jj];
			
	/* Write the array of long ints (after converting to short)        */
	if ( fits_write_img(fptr, TLONG, fpixel, PIXELS_DATA_SIZE, array, &status) )
		return( status );
	fits_close_file(fptr, &status);                  /* close the file */
	fits_report_error(stderr, status); /* print out any error messages */
	
	return status;
}


