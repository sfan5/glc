#!/bin/bash -e
#
# encode-webm.sh -- encoding glc stream to VP8-encoded video
# Copyright (C) 2007-2008 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

FILE=""

AUDIO="1"
VIDEO="1"

QUALITY="10"
BITRATE="6144k"
CPU_USED="2"
MODE="vbr"
VP9_BITRATE="4096k"
VP9_ENABLED="0"

AQUALITY="5"
ABITRATE="160k"
AMODE="vbr"
OPUS_ABITRATE="128k"
OPUS_ENABLED="0"

OUT="video.webm"

ADDOPTS=""

die () {
	echo $1
	exit 1
}

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
	echo "                             sets CRF in vbr mode, bitrate in abr/cbr mode"
	echo "                              that means -m needs to be passed before -q"
	echo "                             default is ${QUALITY} (vbr) or ${BITRATE} (abr/cbr)"
	echo "  --cpu-used=VAL            cpu-used parameter"
	echo "                             sets encoder speed (values from 0 to 5, from slower to faster)"
	echo "                             0, 1 or 2 is probably what you want here"
	echo "                             default is ${CPU_USED}"
	echo "                             for more information consult the libvpx documentation at"
	echo "                             http://www.webmproject.org/docs/encoder-parameters/#2-encode-quality-vs-speed"
	echo "  --audio-quality=VAL       audio quality parameter"
	echo "                             sets quality in vbr mode, bitrate in cbr mode"
	echo "                              that means --audio-mode needs to be passed before --audio-quality"
	echo "                             default is ${AQUALITY} (vbr) or ${ABITRATE} (cbr)"
	echo "  --opus                    Use opus audio codec"
	echo "                             this flag will switch audio to cbr mode (libopus doesn't support vbr)"
	echo "                             and set a different default bitrate (${OPUS_ABITRATE})"
	echo "                             to change the bitrate pass the --opus flag before --audio-quality"
	echo "  --vp9                     Use VP9 video codec"
	echo "                             this flag will set a different default bitrate (${VP9_BITRATE})"
	echo "                             to change the bitrate pass the --vp9 flag before -q"
	echo "  -x, --addopts=OPTS        additional ffmpeg options"
	echo "  -h, --help                show this help"
}

OPT_TMP=`getopt -o o:v:a:m:q:x:h -l out:,video:,audio:,mode:,audio-mode:,quality:,cpu-used:,audio-quality:,opus,vp9,addopts: \
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
		-v|--video)
			VIDEO="$2"
			shift 2
			;;
		-a|--audio)
			AUDIO="$2"
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
		--cpu-used)
			CPU_USED="$2"
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
		--opus)
			ABITRATE="${OPUS_ABITRATE}"
			AMODE="cbr"
			OPUS_ENABLED="1"
			shift 1
			;;
		--vp9)
			BITRATE="${VP9_BITRATE}"
			VP9_ENABLED="1"
			shift 1
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

CORES=$(cat /proc/cpuinfo | grep processor | wc -l)


AUDIOTMP="_tmp_audio$$.ogg"
ENCOPTS="-quality good -cpu-used $CPU_USED -threads $CORES -b:v $BITRATE"
[ $VP9_ENABLED -eq 1 ] \
	&& ENCOPTS="-c:v libvpx-vp9 $ENCOPTS" \
	|| ENCOPTS="-c:v libvpx $ENCOPTS"

AENCOPTS="-c:a libvorbis"
[ $OPUS_ENABLED -eq 1 ] && AENCOPTS="-c:a libopus"

if [ "$MODE" == "vbr" ]; then
	ENCOPTS="$ENCOPTS -crf $QUALITY"
elif [ "$MODE" == "abr" ]; then
	true
elif [ "$MODE" == "cbr" ]; then
	ENCOPTS="$ENCOPTS -minrate $BITRATE -maxrate $BITRATE"
else
	die "Invalid video bitrate mode."
fi

if [ "$AMODE" == "vbr" ]; then
	[ $OPUS_ENABLED -eq 1 ] && die "Opus does not support vbr mode"
	AENCOPTS="$AENCOPTS -q:a $AQUALITY"
elif [ "$AMODE" == "cbr" ]; then
	AENCOPTS="$AENCOPTS -b:a $ABITRATE"
else
	die "Invalid audio bitrate mode"
fi

glc-play "${FILE}" -o - -a "${AUDIO}" | ffmpeg -i - $AENCOPTS "${AUDIOTMP}"

glc-play "${FILE}" -o - -y "${VIDEO}" | \
	ffmpeg -i - -i "${AUDIOTMP}" -c:a copy $ENCOPTS $ADDOPTS -y "${OUT}"

rm -f "${AUDIOTMP}"
