#!/bin/bash

num=1 
while [ $num -le 720 ]; do
	clear
	timeout 5 ps -u $USER
	sleep 5
	num=$(($num+1))
done
