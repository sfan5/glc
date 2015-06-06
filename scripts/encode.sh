#!/bin/bash -e
#
# encode.sh -- encoding glc stream to x264-encoded video
# Copyright (C) 2007-2008 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

FILE=""

AUDIO="1"
VIDEO="1"

QUALITY="19"
BITRATE="6500k"
MODE="vbr"

AQUALITY="1"
ABITRATE="224k"
AMODE="vbr"

OUTFMT="mp4"
OUT="video.${OUTFMT}"

ADDOPTS=""

showhelp () {
	echo "$0 [option]... [glc stream file]"
	echo "  -o, --out=FILE            write video to FILE"
	echo "                             default is ${OUT}"
	echo "  -v, --video=NUM           video stream number"
	echo "                             default is ${VIDEO}"
	echo "  -a, --audio=NUM           audio stream number"
	echo "                             default is ${AUDIO}"
	echo "  -m, --mode=MODE           video bitrate mode (vbr, abr or cbr)"
	echo "                             default mode is ${MODE}"
	echo "  --audio-mode=MODE         audio bitrate mode (vbr or cbr)"
	echo "                             default mode is ${AMODE}"
	echo "  -q, --quality=VAL         video quality parameter"
	echo "                             sets CRF in vbr mode, bitrate in cbr mode"
	echo "                             default is ${QUALITY} (vbr) or ${BITRATE} (cbr)"
	echo "  --audio-quality=VAL       audio quality parameter"
	echo "                             sets quality in VBR mode, bitrate in cbr mode"
	echo "                             default is ${AQUALITY} (vbr) or ${ABITRATE} (cbr)"
	echo "  -f, --outfmt=FORMAT       output container format"
	echo "                             default is ${OUTFMT}"
	echo "  -x, --addopts=OPTS        additional ffmpeg options"
	echo "  -h, --help                show this help"
}

OPT_TMP=`getopt -o o:v:a:m:q:f:x:h -l out:,video:,audio:,mode:,audio-mode:,quality:,audio-quality:,outfmt:,addopts: \
	-n "$0" -- "$@"`
if [ $? != 0 ]; then showhelp; exit 1; fi

eval set -- "$OPT_TMP"

while true; do
	case "$1" in
		-o|--out)
			OUT="$2"
			shift 2
			;;
		-m|--mode)
			MODE="$2"
			shift 2
			;;
		--audio-mode)
			AMODE="$2"
			shift 2
			;;
		-q|--quality)
			if [ ${MODE} == vbr ]; then
				QUALITY="$2"
			else # abr and cbr
				BITRATE="$2"
			fi
			shift 2
			;;
		--audio-quality)
			if [ ${AMODE} == vbr ]; then
				AQUALITY="$2"
			else # cbr
				ABITRATE="$2"
			fi
			shift 2
			;;
		-v|--video)
			VIDEO="$2"
			shift 2
			;;
		-a|--audio)
			AUDIO="$2"
			shift 2
			;;
		-f|--outfmt)
			OUTFMT="$2"
			shift 2
			;;
		-x|--addopts)
			ADDOPTS="$2"
			shift 2
			;;
		-h|--help)
			showhelp
			exit 0
			shift 2
			;;
		--)
			shift
			break
			;;
		*)
			echo "Unrecognized option: $1"
			showhelp
			exit 1
			;;
	esac
done

for arg do FILE=$arg; done
if [ "$FILE" == "" ]; then
	showhelp
	exit 1
fi

[ "$OUTFMT" == "mp4" ] && ADDOPTS="$ADDOPTS -movflags +faststart"

AUDIOTMP="_tmp_audio$$.mp3"
ENCOPTS="-c:v libx264 -preset slow"
AENCOPTS="-c:a libmp3lame"

if [ "$MODE" == "vbr" ]; then
	ENCOPTS="$ENCOPTS -crf $QUALITY"
elif [ "$MODE" == "abr" ]; then
	ENCOPTS="$ENCOPTS -b:v $BITRATE"
elif [ "$MODE" == "cbr" ]; then
	ENCOPTS="$ENCOPTS -b:v $BITRATE -minrate $BITRATE -maxrate $BITRATE -bufsize 1024k"
else
	echo "Invalid video bitrate mode."
	exit 1
fi

if [ "$AMODE" == "vbr" ]; then
	AENCOPTS="$AENCOPTS -q:a $AQUALITY"
elif [ "$AMODE" == "cbr" ]; then
	AENCOPTS="$AENCOPTS -b:a $ABITRATE"
else
	echo "Invalid audio bitrate mode."
	exit 1
fi

glc-play "${FILE}" -o - -a "${AUDIO}" | ffmpeg -i - $AENCOPTS "${AUDIOTMP}"

glc-play "${FILE}" -o - -y "${VIDEO}" | \
	ffmpeg -i - -i "${AUDIOTMP}" -c:a copy $ENCOPTS $ADDOPTS -y "${OUT}"

rm -f "${AUDIOTMP}"
