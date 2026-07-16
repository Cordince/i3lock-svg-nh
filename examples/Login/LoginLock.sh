#!/bin/bash
# Change proj_dir below to your project directory, then save this file as /usr/bin/i3lock,
# overriding the package-installed version on fc37.
DIR=/home/nharrington/projects/i3lock-svg-nh
IMG=$DIR/examples/Login

$DIR/i3lock \
  -i $IMG/Login_Background_1920x1080_dark_letters.png \
  -s $IMG/Login_i3lock.svg
