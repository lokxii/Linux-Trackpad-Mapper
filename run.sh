#!/bin/bash

xhost local:root
swaymsg input 1452:850:Apple_MTP_multi-touch events disabled

sudo -E ./main

swaymsg input 1452:850:Apple_MTP_multi-touch events enabled
