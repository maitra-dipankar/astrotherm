#!/usr/bin/env python3

'''
Shows how to median combine a bunch of dark FITS-format frames taken
by the ThermApp camera + astrotherm code, and then subtract the
median combined dark 'masterdark' from a raw, science image. 
Optionally the pixel values of the dark-subtracted image is adjusted so
that the minimum pixel value of the dark-subtracted image is zero.

TBD: Write masterdark and dark-subtracted images in FITS format. Make sure
the DATE-OBS and other crucial keyworks of the raw frames are preserved in
the dark-subtracted images.
'''

import glob
import numpy as np
from astropy.io import fits
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1 import make_axes_locatable


def median_combine_darks (darkpath):
    '''
    Parameters
    ----------
    darkpath : path to the folder that contains *dark*fits frames

    Returns
    -------
    masterdark : numpy 2D array
    '''
    darks = sorted(glob.glob(darkpath + "*dark*.fits"))
    ndarks = len(darks)
    darkdata = np.zeros((ndarks, 288, 384))
    
    for ii in range(ndarks):
        print('Loading dark: ',darks[ii])
        darkdata[ii] = fits.getdata(darks[ii], ext=0)
        
    masterdark = np.median(darkdata, axis=0)
    return masterdark

def dark_subtract(raw, dark, leveladjust=False):
    '''
    Subtracts a dark image array from a raw image array. If leveladjust
    is True then the pixel values of the dark subtracted image are adjusted
    so that the minimum pixel value of the dark-subtracted image is zero.

    Parameters
    ----------
    raw : 2D numpy array 
    dark : 2D numpy array
    leveladjust : Boolean

    Returns
    -------
    Dark-subtracted 2D array.

    '''
    imred = raw - dark
    
    if leveladjust is True:
        imMin = np.amin(imred)
        imred-= imMin
        
    return imred


# Median combine the darks to create a 'masterdark'
darkpath="/home/dmaitra/Desktop/test_thermapp/therm2022-02-26/"
master_dark = median_combine_darks(darkpath)


# Load a raw science image
rawfits = darkpath + 'thermapp_20220226_205938.fits'
rawdata = fits.getdata(rawfits, ext=0)


# Subtract the masterdark from the raw image, and adjust levels so that 
# the minimum pixel value of the dark-subtracted image is zero.
imred = dark_subtract(rawdata, master_dark, True)


# Visualize results
cmapt="magma"
plot_titles = ['Raw image', 'Median combined dark', 'Dark subtracted']
imstack = np.stack((rawdata, master_dark, imred))

fig, axs = plt.subplots(1, 3, figsize=(15, 5))

for ii in range(3):
    axs[ii].set_title(plot_titles[ii])
    im = axs[ii].imshow(imstack[ii], interpolation='None', cmap=cmapt)
    divider = make_axes_locatable(axs[ii])
    cax = divider.append_axes('bottom', size='5%', pad=0.05)
    fig.colorbar(im, cax=cax, orientation='horizontal')
    axs[ii].axis('off')
    

plt.savefig("dark_subtraction.png", dpi=120, bbox_inches='tight')
plt.show()