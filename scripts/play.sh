#!/bin/bash
#
# play.sh -- playing glc stream with mpv
# Copyright (C) 2007 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

VIDEO="1"
AUDIO="1"

AUDIOFIFO="/tmp/glc-audio$$.fifo"
VIDEOFIFO="/tmp/glc-video$$.fifo"

if [ "$1" == "" ]; then
	echo "Usage: $0 FILE [video ID] [audio ID]"
	exit 1
fi

[ "$2" != "" ] && VIDEO=$2
[ "$3" != "" ] && AUDIO=$3

mkfifo "${AUDIOFIFO}"
mkfifo "${VIDEOFIFO}"

glc-play "$1" -o "${AUDIOFIFO}" -a "${AUDIO}" &
glc-play "$1" -o "${VIDEOFIFO}" -y "${VIDEO}" &

mpv --audio-file "${AUDIOFIFO}" "${VIDEOFIFO}"

rm -f "${AUDIOFIFO}" "${VIDEOFIFO}"
