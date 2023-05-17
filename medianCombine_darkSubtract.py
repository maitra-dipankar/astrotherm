#!/usr/bin/env python3

'''
Shows how to median combine a bunch of dark FITS-format frames taken
by the ThermApp camera + astrotherm code, and then subtract the
median combined dark 'masterdark' from a raw, science image. 
Optionally the pixel values of the dark-subtracted image is adjusted so
that the minimum pixel value of the dark-subtracted image is zero.
'''


import glob
import numpy as np
from astropy.io import fits
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1 import make_axes_locatable


# Path to darks, and the raw 'science' FITS frames
darkpath="./"
rawfits = darkpath + 'thermapp_20230517_194046.fits'


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


def visualize_results(ipfitsname, rawArr, mdarkArr, darksubtdArr, opdpi=120):
    '''
    Given the name of the input raw science FITS frame, the 2D array with the
    pixel values in the raw frame, the array with master dark's pixel values, 
    and the array with the values of the dark-subtracted pixels, create a
    PNG file that shows each of these three images.

    Parameters
    ----------
    ipfitsname : string
        DESCRIPTION: full path to the input raw science FITS frame.
    rawArr : 2D numpy array
        DESCRIPTION: array with pixel values of the raw frame.
    mdarkArr : 2D numpy array
        DESCRIPTION: array with pixel values of the master dark frame.
    darksubtdArr : T2D numpy array
        DESCRIPTION: array with pixel values of the dark-subtracted frame.
    opdpi : integer, optional
        DESCRIPTION. DPI of output PNG imageThe default is 120.

    Returns
    -------
    int
        DESCRIPTION: 0 if success. Also creates output PNG image with name
        such that ip.fits becomes ip_ds.png

    '''
    
    op = ipfitsname[:-5] + '_ds.png'   # op = input_ds.png
    cmapt="magma"
    plot_titles = ['Raw image', 'Median combined dark', 'Dark subtracted']
    imstack = np.stack((rawArr, mdarkArr, darksubtdArr))

    fig, axs = plt.subplots(1, 3, figsize=(15, 5))

    for ii in range(3):
        axs[ii].set_title(plot_titles[ii])
        im = axs[ii].imshow(imstack[ii], interpolation='None', cmap=cmapt)
        divider = make_axes_locatable(axs[ii])
        cax = divider.append_axes('bottom', size='5%', pad=0.05)
        fig.colorbar(im, cax=cax, orientation='horizontal')
        axs[ii].axis('off')
    
    plt.savefig(op, dpi=opdpi, bbox_inches='tight')
    
    return 0


# Median combine the darks to create a 'masterdark'
master_dark = median_combine_darks(darkpath)


# Load a raw science image
rawdata, rawheader = fits.getdata(rawfits, header=True, ext=0)


# Subtract the masterdark from the raw image, and adjust levels so that 
# the minimum pixel value of the dark-subtracted image is zero.
imred = dark_subtract(rawdata, master_dark, leveladjust=True)


# Write the masterdark and dark-subtracted array as FITS images
op = 'masterdark.fits'
rawheader['IMGTYPE'] = ("MedCombMastDark", "Median combined masterdark")
fits.writeto(op, master_dark, header=rawheader, overwrite=True)

op = rawfits[:-5] + '_ds.fits'   # op = input_ds.fits
rawheader['IMGTYPE'] = ("DarkSubtd", "Dark subtracted frame")
fits.writeto(op, imred, header=rawheader, overwrite=True)

# Uncomment the line below to create a PNG visualization
visualize_results(rawfits, rawdata, master_dark, imred)
