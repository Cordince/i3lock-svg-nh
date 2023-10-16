#!/bin/bash 
# Change proj_dir below to your project directory, then save this file as /usr/bin/i3lock, overriding the package-installed version on fc37. 
DIR=/home/nharrington/projects/i3lock-svg-nh/
IMG=$DIR/examples/Cordince
$DIR/i3lock \
-i $IMG/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png \
`` -s $IMG/Cordince_Organ_Engineering_i3lock.svg 
