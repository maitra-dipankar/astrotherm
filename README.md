# astrotherm
## * Save frames from Therm-App IR camera as FITS images (in C)

## * Median combine dark frames and dark subtract (in Python)

![dark_subtraction](https://user-images.githubusercontent.com/81639258/225170064-442f5523-a59b-4b00-861e-1a6b85b66255.png)


C-code usage (based on [encryptededdy](https://github.com/encryptededdy/ThermAppCam)):
 - First make sure the v4l2loopback kernel module is loaded:
    
    > sudo modprobe v4l2loopback

 - Plug in the camera. Keep the lens covered as you start the software:
    
    > sudo astrotherm

    The software will read NDARKS frames (as defined in main.c) for its 
    automatic calibration. It will also dump these dark frames as FITS images. 
    After that is complete, you may remove the lens cap, open /dev/video0 in 
    your video player of choice, E.g.
    
    > vlc v4l2:///dev/video0
    
    or
    
    > mplayer tv://device=/dev/video
    
    to watch live stream video from the camera.

 - While the live stream video is displayed on vlc or mplayer go to the 
   terminal where you typed 'sudo astrotherm'.
   Pressing s or S will save the instantaneous frame as a FITS image. The 
   name of the FITS will be the UTC time at that moment.

 - Pressing q or Q will cause the code to quit.
--------------------------------------
## Dependencies for the C-codes
* v4l2loopback
* libusb >= 1.0
* cfitsio
## Dependencies for the python code
* numpy, astropy, matplotlib
--------------------------------------
### Notes/References
* 2022-Feb-01 (DM): Rewrote the Makefile to take care of proper linking.
  > make clean
  
  > make

* Non-blocking ncurses getch() based on:
 https://www.raspberrypi.org/forums/viewtopic.php?t=177157 (accessed 2021-Aug-19)

* ThermApp reading based on:
 https://github.com/encryptededdy/ThermAppCam (accessed 2023-Mar-13)

* FITS writing based on:
 https://heasarc.gsfc.nasa.gov/docs/software/fitsio/c/c_user/node17.html (accessed 2023-Mar-13)
