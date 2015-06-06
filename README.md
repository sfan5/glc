# About glc
**glc** is an [ALSA](http://en.wikipedia.org/wiki/Advanced_Linux_Sound_Architecture) & [OpenGL](http://en.wikipedia.org/wiki/OpenGL) capture tool for Linux. It consists of a generic video capture, playback and processing library and a set of tools built around that library. **glc** should be able to capture any application that uses ALSA for sound and OpenGL for drawing. It is still a relatively new project but already has a long list of features.

**glc** is inspired by [yukon](https://devel.neopsis.com/projects/yukon) (another, excellent real-time video capture tool for Linux) and [Fraps](http://en.wikipedia.org/wiki/Fraps) (a popular Windows tool for the same purpose).

# Features / Highlights
 * Open Source and licenced under zlib-style licence.
 * Thread-based architechture takes full advantage of multicore-CPUs.
 * Support for multiple simultaneous audio and video streams.
 * Reads frames asynchronously from GPU using **GL_ARB_pixel_buffer_object** extension.
 * Does enforce fps cap only in the captured stream.
 * If the application can play audio using ALSA, **glc** can record it regardless of sound card's capabilities.
 * Support for recording voice to a separate audio stream.
 * Minimal application overhead (eg. slow HDD does not slow program down).
 * Fast arbitrary ratio video scaling with bilinear filtering.
 * Does colorspace conversion to YCbCr 420jpeg which cuts stream size in half.
 * Compresses stream with lightweight [QuickLZ](http://www.quicklz.com/), [LZO](http://www.oberhumer.com/opensource/lzo/) or [LZJB](http://en.wikipedia.org/wiki/LZJB) compression which saves additional 40%-60%.

# Installing glc
## Arch Linux
There is a [git package](https://gist.github.com/sfan5/0be8046c388db6080d5c) for glc.

## Arch Linux 64bit
Install the following dependencies to allow the install of the 32bit libraries:

	# pacman -S gcc-multilib gcc-libs-multilib binutils-multilib libtool-multilib lib32-glibc

Then install from the [build script](scripts/glc-build.sh):

	$ wget https://github.com/sfan5/glc/raw/master/scripts/glc-build.sh
	$ chmod a+x glc-build.sh
	# ./glc-build.sh

## Ubuntu (and derivatives)
Ubuntu users need following packages installed in order to compile **glc**:

	sudo apt-get install build-essential cmake libx11-dev libxxf86vm-dev libgl1-mesa-dev libasound2-dev libpng12-dev

Then use the [build script](scripts/glc-build.sh) to build and install glc.

## Fedora x86_64
Some additional tricks were required to get glc compiling correctly under Fedora 64-bit. Firstly, you'll need to get the system to stop complaining about gcc-multilib. Multilib should already be installed and working on Fedora, but you'll probably need the 32-bit development tools for the script to compile correctly. The command:

	yum install glibc-devel.i686

should take care of that.

Then, you'll need to grab the 32-bit dependencies listed above in the Ubuntu section; my method was to simply execute:

	yum provides */libGL.so

Or similar for each of the unlocatable dependencies shown after attempting to compile (and shown above) and install the corresponding .i686 package to provide 32-bit variants of the necessary files. Depending on which package you get you might need to add an extra symbolic link or two (I had to for the nvidia version of libGL.so that I grabbed) so check the /usr/lib folder to make sure everything is in its correct place. If you used a non-default installation directory be sure to listen to the installer and add the export lines to your .bashrc so you can run glc from anywhere easily.

## Other distributions
There is a [build script](scripts/glc-build.sh): 

	$ wget https://github.com/sfan5/glc/raw/master/scripts/glc-build.sh
	$ chmod a+x glc-build.sh
	# ./glc-build.sh

# How to capture
## Quickstart
To capture an application, execute

	glc-capture [application to capture]

When you want to start or stop capturing, press **Shift + F8**.

**Note**: if you are capturing threaded windows application, use **wine-pthread** executable.
## Common options
For complete list of available options see
	glc-capture --help

### -o, --out=FILE
Set stream file name. **%d** is expanded to program's pid.

_default: pid-%d.glc_

### -f, --fps=FPS
Capture at **FPS**. Capturing frame rate is independent from application's fps and glc does not block application if system is not ready to capture a frame (eg. HDD is busy).

_default: 30_

### -r, --resize=FACTOR
Multiply frame dimensions by **FACTOR**. For example capturing glxgears (by default 300x300) with **-r 0.5** results 150x150 video stream.

**FACTOR** must be greater than zero. **FACTOR 0.5** uses special scaling path which is significantly faster than others. **FACTOR 1.0** disables scaling.

_default: 1.0_

### -c, --crop=WxH+X+Y
Capture only **W**x**H** pixels starting at **X**,**Y** (measured from upper left corner). If **X** and **Y** are not specified **0,0** is used. If specified area is larger than window, only the intersection is calculated. If **X** or **Y** is larger than window dimensions **0** is used.

### -a, --record-audio=CONFIG
Record specified ALSA devices. **CONFIG** format is **device,rate,channels;device2...**.

### -s, --start
Start capturing as soon as application intializes either ALSA or OpenGL. Use this if hotkey does not work in application (eg. compiz).

### -e, --colorspace=CSP
**glc** supports RGB (**-e bgr**) and YV12 ITU-R BT.601 (**-e 420jpeg**) colorspaces. Framebuffer is in BGR format so conversion to YV12 ITU-R BT.601 requires extra calculations but cuts stream size in half and often results better compression.

_default: 420jpeg_

### -k, --hotkey=HOTKEY
Start or stop capturing when **HOTKEY** is pressed. **<Shift>** and **<Ctrl>** modifiers are supported.

_default: <Shift>F8_

### --no-pbo
Using GL_ARB_pixel_buffer_object is possible to retrieve data from GPU to system memory while application is drawing the next frame. See [NVIDIA's  document](http://http.download.nvidia.com/developer/Papers/2005/Fast_Texture_Transfers/Fast_Texture_Transfers.pdf) about fast texture transfers.

_default: GL_ARB_pixel_buffer_object is used if available_

### -z, --compression=METHOD
**glc** supports stream compression using [QuickLZ](http://www.quicklz.com/) (**quicklz**), [LZO](http://www.oberhumer.com/opensource/lzo/) (**lzo**) or [LZJB](http://en.wikipedia.org/wiki/LZJB) (**lzjb**). Setting this to **none** disables compression.

_default: quicklz_

### --byte-aligned
By default frames are read with GL_PACK_ALIGNMENT 8 which makes pixel rows double-word aligned (this is recommended [here](http://www.opengl.org/resources/faq/technical/performance.htm)). If you wish to use GL_PACK_ALIGNMENT 1 (byte-aligned rows), enable this.

_default: GL_PACK_ALIGNMENT 8_

### -i, --draw-indicator
**glc** can draw a red square at the upper left corner of the window being captured. This does not work when capturing front buffer.

_default: indicator is not drawn_

### -v, --log=LEVEL
Log messages with lesser than, or equal to **LEVEL** level. Levels are
 * 0 - errors
 * 1 - warnings
 * 2 - performance information
 * 3 - information
 * 4 - debug

_default: messages are not logged_

### -l, --log-file=FILE
Write log to **FILE**. Like in **-o**, **%d** is expanded to program's pid.

_default: stderr_

### --audio-skip
Currently audio capture is done via hooking snd_pcm_write*() and snd_pcm_mmap_*() functions. When application sends data to ALSA, **glc** copies it to a temporary location and sends signal to a thread which writes the data to actual capture buffer. If subsequent call to hooked ALSA write function occurs before the thread has finished writing data to the buffer, **glc** either skips new data or waits until the thread has finished depending on whether **--audio-skip** is set.

Since in ALSA's asynchronous mode write calls can occur from signal handlers, **glc** must use [busy waiting](http://en.wikipedia.org/wiki/Busy_waiting) to wait for the thread which inflicts an additional overhead.

_default: audio skipping disabled_

### --disable-audio
Set this to disable audio capture.

_default: audio is enabled_

### --sighandler
Install signal handler to flush capture buffer to disk when application is terminated via Ctrl+C for example.

_default: signal handler is not used_

### -g, --glfinish
By default **glc** reads a frame when glXSwapBuffers() is called. Some applications however (most notably compiz) may sometimes draw directly to the front buffer and not call glXSwapBuffers(). Enabling this option makes **glc** to capture the selected buffer (see --capture) when glFinish() is called. Use this option to capture compiz.

_default: capture when glXSwapBuffers() is called_

### -j, --force-sdl-alsa-drv
Sets SDL_AUDIODRIVER=alsa environment variable. This is just for convenience.

_default: SDL_AUDIODRIVER=alsa is not set_

### -b, --capture=BUFFER
Read frames from either GL_FRONT (**front**) or GL_BACK (**back**).

_default: frames are read from front buffer_

# How to play a captured stream
## Play directly
To play a captured stream directly, execute
	glc-play [stream file]

**ESC** stops playback (closing window just closes a video stream), **f** toggles fullscreen and **Right** seeks forward.
## Play using mpv
By exporting audio and video streams to pipes it is possible to play a stream directly using mpv.

To play just video, execute

	glc-play [stream file] -y 1 -o - | mplayer -demuxer y4m -

To play audio, execute

	glc-play [stream file] -a 1 -o - | mplayer -demuxer lavf -

Using FIFOs it is possible to play both streams simultaneously (with [play.sh](scripts/play.sh)): 

	scripts/play.sh [stream file]

# Frequently Asked Questions (FAQ)
## Can I record audio using OSS?

Yes! You will just have to emulate ALSA. Instructions can be found on the [Arch Linux wiki](https://wiki.archlinux.org/index.php/Open_Sound_System#Instructions).

## Errors
## ERROR: ld.so: object 'libglc-hook.so' from LD_PRELOAD cannot be preloaded: ignored.
You probably used glc-build.sh script to install '''glc''' into non-standard location. Besides correct '''PATH''' environment variable you need to provide '''LD_LIBRARY_PATH''' value. glc-build.sh instructs you about those variables:

```
info  : Done :)
info  : You may need to add following lines to your .bashrc:
export PATH="${PATH}:/home/pyry/tmp/glc/bin"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/home/pyry/tmp/glc/lib64:/home/pyry/tmp/glc/lib32"
```

# Exporting and encoding of captured streams
glc-play is able to export audio streams in [WAV](http://en.wikipedia.org/wiki/WAV) and video streams in [YUV4MPEG](http://wiki.multimedia.cx/index.php?title=YUV4MPEG2), [BMP](http://en.wikipedia.org/wiki/BMP_file_format) or [PNG](http://en.wikipedia.org/wiki/Portable_Network_Graphics) files for transcoding into a commonly playable video format.

Please note that while glc is able to handle multiple dynamic audio and video streams, common video containers and formats usually don't. Each stream must be extracted separately. If stream configuration changes (eg. window was resized when it was captured) glc-play generates a new export file for each configuration (**%d** is usually substituted with export file counter in file name).

To extract information about captured streams (eg. how many video and audio streams it has), execute

	glc-play [stream file] -i LEVEL

where LEVEL is a verbosity level (try numbers between 1 and 6). For example with verbosity level 1, glc-play prints out something like

	[   0.00s] video stream 1
	[   0.00s] audio stream 1
	[  54.87s] end of stream
	video stream 1
	  frames      = 2469
	  fps         = 45.00
	  bytes       = 1.13 GiB
	  bps         = 21.09 MiB
	audio stream 1
	  packets     = 2570
	  pps         = 46.84
	  bytes       = 9.22 MiB
	  bps         = 171.98 KiB

### Video stream (YUV4MPEG)
To export video stream number **NUM**, execute

	glc-play [stream file] -y NUM -o video.y4m

If you have resized the window while capturing, you need to add **%d** to file name to recover the whole stream.

### Video stream (PNG)
	glc-play [stream file] -p NUM -o pic-%010d.png

### Audio stream (WAV)
	glc-play [stream file] -a NUM -o audio.wav

## Encoding
Video encoding is a complex business and if you just want a nice .mp4, use [scripts/encode.sh](scripts/encode.sh) included in the source distribution package.

	./encode.sh [stream file] -o mynicefragvid.mp4

It is possible export audio and video to pipe and tell encoding applications to read from pipe. It is often much faster and we are going to use it in our examples.

## Alternative encoding: quick and dirty
This method uses ffmpeg and assumes that you want to encode only the first audio and video track. It produces a low-quality video, for improving the quality you can try using a higher bitrate.

	glc-play [stream file] -a 1 -o audio.wav
	glc-play [stream file] -y 1 -o - | ffmpeg -i - -i audio.wav -c:v mpeg4 -b:v 4000k -c:a libmp3lame -b:a 192k output.avi
	rm audio.wav

# Bug reporting guide
Writing a good bug report isn't an easy task, especially for non-developers. Please take a look into following lists before submitting a new issue.

## Things to try first
 * Read the FAQ.
 * Remember to either give **-s** option to glc-capture or press the hotkey (Shift+F8).
 * If you are using wine, try wine-pthread executable if such exists.

## Information you must include in bug report
 * glc version (**--version** if you are using newer than 0.5.5).
 * Application which you are capturing. Please include full version.
 * All command line options used.
 * Log file (**-v 5 -l glc.log**).
 * Expected result (eg. application should not crash).

## Information you should include if you can
 * gdb backtrace.
 * valgrind --tool=memcheck output.

# Developer information

## Modules
**glc** consists of main application (_glc_), optional support code (_glc-support_), thread-safe buffer library (_packetstream_) and a library for doing magic with ELF (_elfhacks_). _glc_, _packetstream_ and _elfhacks_ are licenced under zlib-style licence. _glc-support_ has code currently licenced under GPL.

Main application is divided into **glc libraries** (video capturing, processing and playback library), **glc-hook** (hook functions for capturing), **glc-capture** (tool for preloading glc-hook into applications) and **glc-play** (tool for playing or processing streams).

**glc libraries** can be used independently for adding video capture or playback support into applications. Headers are installed into _/usr/include/glc_ by default.

## Source code

### git access
Git is preferred way to acquire source code for development purposes.

```bash
git clone https://github.com/sfan5/glc.git
git submodule init
git submodule update
```

## Required libraries
**glc** requires following libraries:
 * libGL -- rendering library capable of at least OpenGL 1.4
 * libasound -- Recent ALSA libraries
 * libX11 -- X11
 * libXxf86vm -- X11 video mode extension
 * libpng -- PNG library

## Building and installing
cmake is required for building _glc_.

```bash
mkdir -p build
cd build
cmake .. \
	-DCMAKE_INSTALL_PREFIX:PATH="/usr" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_C_FLAGS_RELEASE:STRING="-O2 -fomit-frame-pointer -mtune=generic"
make
sudo make install
```

**Note:** on 64-bit environment 32-bit versions of glc libraries are required for capturing 32-bit applications. To build 32-bit version create a separate build directory, add **-DMLIBDIR="lib32" -DCMAKE_C_FLAGS_RELEASE:STRING="-m32 ..."**

to cmake command options and continue build as usual.

## Licenses
**glc** has [zlib](http://opensource.org/licenses/Zlib) as primary licence. All code in _packetstream_ and _elfhacks_ is under zlib. All mandatory code in _glc_ is under zlib. However there is GPL- and CDDL-licenced code included in _glc_ source package. That code is however optional and can be disabled compile time for binary distributions.

### QuickLZ
QuickLZ (altough rewritten and a bit different algorithm, so technically it is just another RLE+LZ) implementation exists in _support/quicklz_. It is licensed under GPL and can be disabled at compile-time by giving cmake option **-DQUICKLZ:BOOL=OFF**. However author has stated that as long as the QuickLZ part can be easily identified as GPL code and optional at compile-time, it is OK to distribute it.

### LZO
LZO implementation exists in _support/minilzo_. It is licensed under GPL and can be disabled at compile-time by giving cmake option **-DLZO:BOOL=OFF**.

### LZJB
LZJB implementation exists in _support/lzjb_. It is licensed under CDDL and can be disabled at compile-time by giving cmake option **-DLZJB:BOOL=OFF**.
