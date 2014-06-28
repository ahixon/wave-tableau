/*
 * (c) 2014 Alex Hixon
 * <alex@alexhixon.com>
 * 
 * Records and displays an audio stream as an image, in realtime.
 * Requires SDL, PortAudio.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>

#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include <portaudio.h>

#define IMAGE_WIDTH 256
#define IMAGE_HEIGHT 256
#define FLIP 0

//#define DEFAULT_SAMPLE_RATE  (44100)
//#define DEFAULT_SAMPLE_RATE  (96000)
#define DEFAULT_SAMPLE_RATE  (192000)
#define FRAMES_PER_BUFFER (512)
#define NUM_CHANNELS    (2)	// do not change
#define NUM_SECONDS    (3)
#define DITHER_FLAG     (paDitherOff) 

#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE (128)
#define PRINTF_S_FORMAT "%d"

/* simple circular buffer */
typedef struct _AudioBuffer {
    SAMPLE      *elems;
    int         size;
    int         start;
    int         end;
} AudioBuffer;

pthread_mutex_t mutexbuffer;

AudioBuffer* buffer_new (int size) {
    AudioBuffer *buf = malloc (sizeof (struct _AudioBuffer));
    assert (buf);

    buf->size  = size + 1; /* include empty elem */
    buf->start = 0;
    buf->end   = 0;
    buf->elems = calloc (buf->size, sizeof (SAMPLE));
    assert (buf->elems);

    return buf;
}

void buffer_destroy (AudioBuffer *buf) {
    free (buf->elems);
    free (buf);
}

void buffer_write (AudioBuffer *cb, SAMPLE sample) {
    pthread_mutex_lock (&mutexbuffer);
    //printf ("[WRITE] end = %d\n", cb->end);
    cb->elems[cb->end] = sample;
    cb->end = (cb->end + 1) % cb->size;
    if (cb->end == cb->start) {
        cb->start = (cb->start + 1) % cb->size; /* full, overwrite */
    }


    pthread_mutex_unlock (&mutexbuffer);
}

SAMPLE buffer_read (AudioBuffer *cb) {
    pthread_mutex_lock (&mutexbuffer);
    //printf ("[READ] end = %d\n", cb->end);
    SAMPLE elem = cb->elems[cb->start];
    cb->start = (cb->start + 1) % cb->size;
    pthread_mutex_unlock (&mutexbuffer);
    return elem;
}

int buffer_isFull (AudioBuffer *cb) {
    return (cb->end + 1) % cb->size == cb->start;
}
 
int buffer_isEmpty (AudioBuffer *cb) {
    return cb->end == cb->start;
}

int running = 1;


Uint16 CreateHicolorPixel(SDL_PixelFormat * fmt, Uint8 red, Uint8 green,
              Uint8 blue)
{
    Uint16 value;

    /* This series of bit shifts uses the information from the SDL_Format
       structure to correctly compose a 16-bit pixel value from 8-bit red,
       green, and blue data. */
    value = ((red >> fmt->Rloss) << fmt->Rshift) +
    ((green >> fmt->Gloss) << fmt->Gshift) +
    ((blue >> fmt->Bloss) << fmt->Bshift);

    return value;
}

/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    AudioBuffer *buffer = (AudioBuffer*)userData;
    const SAMPLE *incomingSamples = (const SAMPLE*)inputBuffer;

    if (!running) {
        return paComplete;
    }

    if (buffer_isFull (buffer)) {
        printf ("Buffer overflow, skipping bytes\n");
        //return paComplete;
        return paContinue;
    }

    int i;

    for (i = 0; i < framesPerBuffer * NUM_CHANNELS; i++) {
        /*if (buffer_isFull (buffer)) {
            return paComplete;
        }*/

        buffer_write (buffer, incomingSamples[i]);
    }
    
    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    return paContinue;
}

int pixelPos = 0;
SDL_Surface *screen;
int                 dimensions = IMAGE_WIDTH;
int dimensionsx, dimensionsy;

/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int playCallback( const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData )
{
    AudioBuffer *buffer = (AudioBuffer*)userData;
    //SAMPLE *rptr = &data->recordedSamples[data->playedFrameIndex * NUM_CHANNELS];
    SAMPLE *outputSamples = (SAMPLE*)outputBuffer;

    unsigned int i;
    int finished;
    //int framesLeft = data->recordedFrameIndex - data->playedFrameIndex;

    (void) inputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    if (!running) {
        return paComplete;
    }

    Uint16 *raw_pixels;
    int x, y;

    if (buffer_isEmpty (buffer)) {
        printf ("Playback: buffer underflow.\n");
        for (i = 0; i < framesPerBuffer * NUM_CHANNELS; i++) {
            //outputSamples[i] = SAMPLE_SILENCE;
            outputSamples[i] = 0;
        }
    } else {
        SDL_LockSurface(screen);

        /* Get a pointer to the video surface's memory. */
        raw_pixels = (Uint16 *) screen->pixels;

        /* load into our buffer, and draw at the same time */
        for (i = 0; i < framesPerBuffer * NUM_CHANNELS && !buffer_isEmpty (buffer); i++) {
            outputSamples[i] = buffer_read (buffer);
        }

        int useStereo = 1;  // FIXME: pass in
        int pixWidth = useStereo ? 3 : 6;
        int offset;
        for (offset = 0; offset <= i - pixWidth; offset += pixWidth) {
            // we got 3/6 pixel colors, can draw them now

            SAMPLE* drawBuf = outputSamples + offset;
            Uint16 pixel_color;
            int offset;
            if (FLIP) {
                y = dimensionsy - 1 - (pixelPos / dimensionsy);
            } else {
                y = (pixelPos / dimensionsy);
            }

            x = pixelPos % dimensionsx;

            if (y < 0 || x >= dimensionsx || y >= dimensionsy || x < 0) {
                pixelPos = 0;
                SDL_UnlockSurface (screen);
                SDL_Flip (screen);
                return paContinue;

                // if (FLIP) {
                //     y = dimensionsy - 1 - (pixelPos / dimensionsy);
                // } else {
                //     y = (pixelPos / dimensionsy);
                // }
                // x = pixelPos % dimensionsx;
                //break;
            }

            if (useStereo) {
                pixel_color = CreateHicolorPixel(screen->format, (unsigned char)drawBuf[0], (unsigned char)drawBuf[1], (unsigned char)drawBuf[2]);
            } else {
                pixel_color = CreateHicolorPixel(screen->format, (unsigned char)drawBuf[0], (unsigned char)drawBuf[2], (unsigned char)drawBuf[4]);
            }

            offset = (screen->pitch / 2 * y + x);
            raw_pixels[offset] = pixel_color;

            pixelPos ++;
        }

        SDL_UnlockSurface(screen);
        SDL_Flip(screen);
    }

    return paContinue;
}

int main (int argc, char* argv[]) {
    PaStreamParameters  inputParameters,
                        outputParameters;
    PaStream*           inStream;
    PaStream*           outStream;
    PaError             err = paNoError;
    AudioBuffer         *data;
    int                 i;
    int                 totalFrames;
    int                 numSamples;
    int                 numBytes;
    SAMPLE              max, val;
    double              average;
    int                 sdlFlags = SDL_DOUBLEBUF;
    int                 sampleRate = DEFAULT_SAMPLE_RATE;
    int                 useStereo = 0;
    char                c;

    while ((c = getopt (argc, argv, "fr:sd:")) != -1) {
        switch (c) {
            case 'f':
                sdlFlags |= SDL_FULLSCREEN;
                break;
            case 'r':
                if (sscanf (optarg, "%d", &sampleRate) != 1) {
                    fprintf (stderr, "invalid sample rate %s\n", optarg);
                    return EXIT_FAILURE;
                }

                break;
            case 'd':
                if (sscanf (optarg, "%d", &dimensions) != 1) {
                    fprintf (stderr, "invalid dimensions %sx%s\n", optarg);
                    return EXIT_FAILURE;
                }

                break;
            case 's':
                useStereo = 1;
                break;
            case '?':
                return EXIT_FAILURE;
            default:
                abort();
        }
    }

    dimensionsx = IMAGE_WIDTH;
    dimensionsy = IMAGE_HEIGHT;

    printf ("Sample rate: %d\n", sampleRate);
    printf ("Read stereo channel for bitmap? %d\n", useStereo);
    printf ("Fullscreen? %d\n", sdlFlags & SDL_FULLSCREEN);
    printf ("Image dimensions: %dx%d\n", dimensionsx, dimensionsy);

    totalFrames = NUM_SECONDS * sampleRate;
    numSamples = totalFrames * NUM_CHANNELS;

    pthread_mutex_init(&mutexbuffer, NULL);

    data = buffer_new (numSamples);
    if (!data) {
        printf("Could not allocate audio buffer.\n");
        goto done;
    }

    /* setup screen */
    if (SDL_Init (SDL_INIT_VIDEO) != 0) {
        printf("Unable to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    /* Make sure SDL_Quit gets called when the program exits! */
    atexit(SDL_Quit);

    /* no cursor pls */
    SDL_ShowCursor (0);

    screen = SDL_SetVideoMode(dimensionsx, dimensionsy, 16, sdlFlags);
    if (screen == NULL) {
        printf("Unable to set video mode: %s\n", SDL_GetError());
        return 1;
    }

    err = Pa_Initialize ();
    if (err != paNoError) goto done;

    inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        goto done;
    }
    inputParameters.channelCount = 2;                    /* stereo input */
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    /* Record some audio. -------------------------------------------- */
    err = Pa_OpenStream(
              &inStream,
              &inputParameters,
              NULL,                  /* &outputParameters, */
              sampleRate,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              recordCallback,
              data );
    if( err != paNoError ) goto done;

    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    if (outputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default output device.\n");
        goto done;
    }
    outputParameters.channelCount = 2;                     /* stereo output */
    outputParameters.sampleFormat =  PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
              &outStream,
              NULL, /* no input */
              &outputParameters,
              sampleRate,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              playCallback,
              data );
    if( err != paNoError ) goto done;

    err = Pa_StartStream( inStream );
    if( err != paNoError ) goto done;

    if( outStream )
    {
        printf ("Starting output stream too...\n");
        err = Pa_StartStream( outStream );
        if( err != paNoError ) goto done;
    }

    /* keep drawing until quit */
    SDL_Event event;
    int quit = 0;

    while (!quit) {
        int lastUpdate = 0;
        int pos;
        int pixelPos = 0;
        while( ( err = Pa_IsStreamActive( inStream ) ) == 1 )
        {
            Pa_Sleep(100);

            /* check for quit event */
            while( SDL_PollEvent( &event ) ){
                if (event.type == SDL_KEYDOWN) {
                    SDL_KeyboardEvent *key = &event.key;
                    if( key->keysym.unicode < 0x80 && key->keysym.unicode > 0){
                        char keyChar = (char)key->keysym.unicode;
                        printf ("Pressed: %c\n", keyChar);

                        if (keyChar == 'q') {
                            goto doneOK;
                        }
                    } else if (key->keysym.sym == SDLK_ESCAPE) {
                        goto doneOK;
                    } else if (key->keysym.sym == 'q') {
                        // TODO: check docs... this shouldn't work but it does??
                        goto doneOK;
                    } else {
                        printf ("Some other event %d\n", key->keysym.sym);
                    }
                }
            }
        }

        if( err < 0 ) goto done;

    }

doneOK:
    running = 0;    // this will cause streams to stop

    err = Pa_CloseStream( inStream );
    if( err != paNoError ) goto done;

    if (outStream) {
        printf("Waiting for playback to finish.\n"); fflush(stdout);

        while( ( err = Pa_IsStreamActive( outStream ) ) == 1 ) Pa_Sleep(100);
        if( err < 0 ) goto done;
        
        err = Pa_CloseStream( outStream );
        if( err != paNoError ) goto done;
        
        printf("Done.\n"); fflush(stdout);
    }

done:
    Pa_Terminate();
    pthread_mutex_destroy(&mutexbuffer);
    buffer_destroy (data);
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          /* Always return 0 or 1, but no other return codes. */
    }
    return err;
}

