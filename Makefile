
CC=gcc
CFLAGS=-std=c99 -g -Wall
LDFLAGS=-lgps -lconfig -pthread

all: dumb_spectrum_monitor


install-service: dumb_spectrum_monitor.service
	install dumb_spectrum_monitor.service /etc/systemd/system 
	systemctl daemon-reload 

