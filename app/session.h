#pragma once

#include <stdint.h>

enum { REMOTE_PLAYERS = 4, REMOTE_ADDRESS_BYTES = 6 };

enum SessionMessageType {
  REMOTE_CONNECTED = 1,
  REMOTE_DISCONNECTED,
  REMOTE_INPUT,
  REMOTE_OUTPUT,
  SELECT_SONG,
  LIBRARY_READY,
  GAME_VISIBLE,
  DANCE_STARTED,
  RECAP_ENTERED,
  TEST_BUTTONS,
  USB_DISCOVERY,
};

typedef struct {
  uint8_t type, size, player;
  uint8_t data[32];
} SessionMessage;

int session_send(int socket, enum SessionMessageType type, const void *data, unsigned size);
int session_send_player(int socket, enum SessionMessageType type, unsigned player,
                        const void *data, unsigned size);
int session_read(int socket, SessionMessage *message, int flags);
