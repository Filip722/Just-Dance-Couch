#pragma once

#include <pthread.h>

/* The USB session owns a socket carrying the same report IDs as the game worker. */
int usb_remote_start(pthread_t *thread);
void usb_remote_stop(int fd, pthread_t thread);
