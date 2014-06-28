wave-tableau
============

Convert images or video (file/webcam) to sound and back to a video image in real-time.

Uses PortAudio for audio recording/playback, and SDL for displaying the image.

Currently Linux only, but easily portable to Windows/OS X (I hope..?).

Installation
------------

On Debian/Ubuntu:

    sudo apt-get install portaudio19-dev libsdl-dev # needs the newer version of portaudio

Compiling
---------
Just use `make`. It's that easy.

Using
-----

Create some input bitmap stream, and use:

    ./record

Hit ESC or q to quit.

It will record from the default PortAudio device, and playback what is being recorded/drawn to the screen on the default PortAudio playback device. 

If you need to change this, again, you need to edit the source code. Eventually, some 'list devices' and 'set device' command line flags would be nice.

Currently supported flags:
* `-f` makes a fullscreen window.
* `-r` sets the sample rate
* `-s` used to turn on reading the incoming audio as stereo, rather than mono. At the moment, this is always enabled. If you need to change this, edit the source.
* `-d` used to set image dimensions. Again, this ended up being hard coded. This would be a simple fix to get working again.

Creating bitmap streams
-----------------------

For all the pipelines listed below, `-c 2` indicates a stereo stream. You may also wish to use `-D <ALSADEV>` to output to a particular device, if the default playback device isn't where you want your wave blast to go.

A common method of playback is to send the bitmap stream out of a particular headphone out port, possibly out into some external device, then into line-in to be recorded by `record`, which would then play through line-out. You might need to be crafty if you only have one soundcard and want to attempt some live conversions.

### Bitmap ###

Ensure you save with BGR24 mode (ie 24-bit color, RGB color space, little endian) and set to the correct size.
Then you can just do:

    aplay -t raw <FILENAME> -r <SAMPLERATE>

You will need to set #define FLIP to 1 since the bitmap is encoded with pixels starting at the bottom, not the top.

Alternatively, you can convert any image to raw pixel data:

    ffmpeg -i <FILENAME> -s <IMGW>x<IMGH> -pix_fmt bgr24 -f rawvideo - | aplay -t raw - -r <SAMPLERATE> -c 2

### Webcam ###

Same idea:

    ffmpeg -f video4linux2 -i /dev/video0 -s <IMGW>x<IMGH> -vf scale=<IMGW>:<IMGH> -r <FPS> -pix_fmt bgr24 -f rawvideo - | aplay -t raw - -r <SAMPLERATE> -c 2
    
I set FPS experimentally -- lowering it if frames were being buffered too much (ie you get delay) or increasing it if there was buffer underrun.

See previous sections regarding sample rate and image size decisions.


To list formats supported by your webcam, use:

    avconv -f video4linux2 -list_formats all -i /dev/video0

### Video ###

Again, very similar:

    ffmpeg -i <file> -s <IMGW>x<IMGH> -vf scale=<IMGW>:<IMGH> -r <FPS> -pix_fmt bgr24 -f rawvideo - | aplay -t raw - -r <SAMPLERATE> -c 2
    
Known bugs
----------

At the moment, there seems to be an off-by-one error somewhere, either in PortAudio or, more likely, my buffering code somewhere. Each audio buffer is missing one pixel, which means the image has a tendency to shift after each row.

It kind of looks as though somebody changed the V or H-Shift knob on an old CRT TV. People remember those... right? Right?!

Everything else, please report it on the bug tracker!

If you use this...
------------------

... please let me know! Just file a bug in the tracker, or send me an email. 

I'd love to hear from you, and what you've managed to do with this tool!
