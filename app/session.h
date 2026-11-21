#pragma once

#include <stdint.h>

enum SessionMessageType {
  REMOTE_CONNECTED = 1,
  REMOTE_DISCONNECTED,
  REMOTE_INPUT,
  REMOTE_OUTPUT,
  DANCE_STARTED,
  RECAP_ENTERED,
  TEST_BUTTONS,
};

typedef struct {
  uint8_t type, size;
  uint8_t data[32];
} SessionMessage;

int session_send(int socket, enum SessionMessageType type, const void *data, unsigned size);
int session_read(int socket, SessionMessage *message, int flags);
