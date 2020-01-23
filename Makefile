
CC=gcc
CFLAGS=-std=c99 -g -Wall
LDFLAGS=-lgps -lconfig -pthread

all: pi_rfi_checker


install-service: pi_rfi_checker.service
	install pi_rfi_checker.service /etc/systemd/system 
	systemctl daemon-reload 

