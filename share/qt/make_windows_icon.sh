#!/bin/bash
# create multiresolution windows icon
ICON_DST=../../src/qt/res/icons/MiracleCoin.ico

convert ../../src/qt/res/icons/MiracleCoin-16.png ../../src/qt/res/icons/MiracleCoin-32.png ../../src/qt/res/icons/MiracleCoin-48.png ${ICON_DST}
