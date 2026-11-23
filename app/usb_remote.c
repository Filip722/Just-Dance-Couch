#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bluetooth_psm.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop_posix.h"
#include "btstack_tlv_posix.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hci_transport_usb.h"
#include "l2cap.h"
#include "paths.h"
#include "session.h"
#include "usb_remote.h"

enum {
  NINTENDO = 0x057e, WII_MODULE = 0x0305, HID_INPUT = 0xa1, HID_OUTPUT = 0xa2,
  WII_REMOTE_CLASS = 0x002504, WII_REMOTE_PLUS_CLASS = 0x000508,
  REPORT_MTU = 64, OUTPUT_QUEUE = 32, REQUEST_STATUS = 0x15, INPUT_STATUS = 0x20,
  INQUIRY_UNIT_MS = 1280, PAIR_INQUIRY_UNITS = 4,
  RETRY_MS = 1000, BONDED_LISTEN_MS = PAIR_INQUIRY_UNITS * INQUIRY_UNIT_MS,
  SETUP_TIMEOUT_MS = 12000, STATUS_TIMEOUT_MS = 1800,
  PERIPHERAL_TYPE_MASK = 0x001f3c,
};

typedef struct {
  bd_addr_t address;
  hci_con_handle_t handle;
  uint16_t control, interrupt;
  int control_open, interrupt_open, player, outgoing, alternate_pin, pin_requested;
  SessionMessage queue[OUTPUT_QUEUE];
  unsigned head, count;
  btstack_timer_source_t timeout;
} WiiRemote;

/* All Bluetooth state is used by the BTstack thread only. */
static struct {
  int socket, working, stopping, inquiry, naming, trace, discovery, general_inquiry;
  bd_addr_t candidate, retry_address, pin;
  uint32_t candidate_class;
  uint16_t clock_offset;
  uint8_t page_mode;
  int candidate_named;
  btstack_tlv_posix_t store;
  const btstack_tlv_t *tlv;
  char key_path[PATH_MAX];
  WiiRemote remotes[REMOTE_PLAYERS];
  btstack_timer_source_t scan_timer;
} usb;

static int address_set(const bd_addr_t address) {
  static const bd_addr_t zero;
  return memcmp(address, zero, sizeof zero) != 0;
}

static int is_remote(const void *name, unsigned size, uint32_t device_class) {
  char text[249];
  if (size >= sizeof text) size = sizeof text - 1;
  memcpy(text, name, size);
  text[size] = 0;
  if (strcasestr(text, "RVL-WBC") || strcasestr(text, "RVL-CNT-01-UC") ||
      strcasestr(text, "Balance Board") || strcasestr(text, "Wii U Pro")) return 0;
  return strcasestr(text, "RVL-CNT-01") || strcasestr(text, "Wii Remote") ||
         strcasestr(text, "Wiimote") ||
         device_class == WII_REMOTE_CLASS || device_class == WII_REMOTE_PLUS_CLASS;
}

static int has_bond(bd_addr_t address) {
  link_key_t key;
  link_key_type_t type;
  int found = gap_get_link_key_for_bd_addr(address, key, &type);
  explicit_bzero(key, sizeof key);
  return found;
}

static WiiRemote *by_address(const bd_addr_t address) {
  for (unsigned i = 0; i < REMOTE_PLAYERS; i++)
    if (address_set(usb.remotes[i].address) &&
        !memcmp(usb.remotes[i].address, address, REMOTE_ADDRESS_BYTES)) return &usb.remotes[i];
  return NULL;
}

static WiiRemote *by_channel(uint16_t cid) {
  for (unsigned i = 0; cid && i < REMOTE_PLAYERS; i++)
    if (usb.remotes[i].control == cid || usb.remotes[i].interrupt == cid) return &usb.remotes[i];
  return NULL;
}

static WiiRemote *by_player(int player) {
  for (unsigned i = 0; i < REMOTE_PLAYERS; i++)
    if (address_set(usb.remotes[i].address) && usb.remotes[i].player == player) return &usb.remotes[i];
  return NULL;
}

static WiiRemote *free_peer(void) {
  for (unsigned i = 0; i < REMOTE_PLAYERS; i++)
    if (!address_set(usb.remotes[i].address)) return &usb.remotes[i];
  return NULL;
}

static int can_discover(void) {
  return usb.discovery && !usb.stopping && free_peer() && !by_player(-1);
}

static int select_module(void) {
  libusb_context *context;
  if (libusb_init(&context) != 0) return 0;
  libusb_device **devices;
  ssize_t count = libusb_get_device_list(context, &devices);
  uint8_t bus = 0, path[8];
  int matches = 0, ports = 0;
  for (ssize_t i = 0; i < count; i++) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(devices[i], &desc) != 0 ||
        desc.idVendor != NINTENDO || desc.idProduct != WII_MODULE) continue;
    matches++;
    bus = libusb_get_bus_number(devices[i]);
    ports = libusb_get_port_numbers(devices[i], path, sizeof path);
  }
  if (count >= 0) libusb_free_device_list(devices, 1);
  libusb_exit(context);
  if (matches != 1 || ports <= 0) {
    fprintf(stderr, "usb-wii: expected one Nintendo USB module (057e:0305), found %d\n", matches);
    return 0;
  }
  hci_transport_usb_set_bus_and_path(bus, ports, path);
  return 1;
}

static int open_keys(void) {
  bd_addr_t local;
  gap_local_bd_addr(local);
  reverse_bd_addr(local, usb.pin);
  char directory[PATH_MAX];
  if (!root_path(directory, sizeof directory, "XDG_DATA_HOME", ".local/share") ||
      !directory_tree(directory)) return 0;
  if (snprintf(usb.key_path, sizeof usb.key_path, "%s/usb-wii-%s.tlv", directory,
               bd_addr_to_str_with_delimiter(local, '-')) >= (int)sizeof usb.key_path) return 0;
  int fd = open(usb.key_path, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return 0;
  int ok = fchmod(fd, 0600) == 0;
  close(fd);
  if (!ok) return 0;
  usb.tlv = btstack_tlv_posix_init_instance(&usb.store, usb.key_path);
  hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(usb.tlv, &usb.store));
  return 1;
}

static void dial(const bd_addr_t address, int alternate_pin);

static void scan(btstack_timer_source_t *timer) {
  (void)timer;
  if (!can_discover() || usb.inquiry || usb.naming) return;
  if (address_set(usb.retry_address)) {
    bd_addr_t address;
    memcpy(address, usb.retry_address, sizeof address);
    memset(usb.retry_address, 0, sizeof usb.retry_address);
    dial(address, 1);
    return;
  }
  memset(usb.candidate, 0, sizeof usb.candidate);
  usb.candidate_named = 0;
  gap_inquiry_set_lap(usb.general_inquiry ? GAP_IAC_GENERAL_INQUIRY : GAP_IAC_LIMITED_INQUIRY);
  int status = gap_inquiry_start(PAIR_INQUIRY_UNITS);
  usb.inquiry = status == ERROR_CODE_SUCCESS;
  if (usb.trace)
    fprintf(stderr, "usb-wii: %s inquiry start (0x%02x)\n", usb.general_inquiry ? "general" : "limited", status);
  if (status) {
    btstack_run_loop_set_timer(&usb.scan_timer, RETRY_MS);
    btstack_run_loop_add_timer(&usb.scan_timer);
  }
}

static void rescan(void) {
  btstack_run_loop_remove_timer(&usb.scan_timer);
  if (!can_discover()) return;
  btstack_link_key_iterator_t iterator;
  int paired = 0;
  if (gap_link_key_iterator_init(&iterator)) {
    bd_addr_t address;
    link_key_t key;
    link_key_type_t type;
    paired = gap_link_key_iterator_get_next(&iterator, address, key, &type);
    explicit_bzero(key, sizeof key);
    gap_link_key_iterator_done(&iterator);
  }
  btstack_run_loop_set_timer(&usb.scan_timer,
                            paired && !address_set(usb.retry_address) ? BONDED_LISTEN_MS : RETRY_MS);
  btstack_run_loop_add_timer(&usb.scan_timer);
}

static void lost(WiiRemote *remote) {
  if (remote->player >= 0) {
    session_send_player(usb.socket, REMOTE_DISCONNECTED, remote->player, NULL, 0);
    fprintf(stderr, "usb-wii: Player %d disconnected\n", remote->player + 1);
  }
  btstack_run_loop_remove_timer(&remote->timeout);
  memset(remote, 0, sizeof *remote);
  rescan();
}

static void disconnect_remote(WiiRemote *remote) {
  hci_con_handle_t handle = remote->handle;
  lost(remote);
  if (handle != HCI_CON_HANDLE_INVALID) gap_disconnect(handle);
}

static void setup_timed_out(btstack_timer_source_t *timer) {
  WiiRemote *remote = btstack_run_loop_get_timer_context(timer);
  fprintf(stderr, "usb-wii: remote setup timed out\n");
  disconnect_remote(remote);
}

static void set_timeout(WiiRemote *remote, unsigned milliseconds) {
  btstack_run_loop_remove_timer(&remote->timeout);
  btstack_run_loop_set_timer(&remote->timeout, milliseconds);
  btstack_run_loop_add_timer(&remote->timeout);
}

static WiiRemote *new_peer(const bd_addr_t address) {
  WiiRemote *remote = free_peer();
  if (!remote) return NULL;
  memcpy(remote->address, address, sizeof remote->address);
  remote->player = -1;
  remote->handle = HCI_CON_HANDLE_INVALID;
  btstack_run_loop_set_timer_handler(&remote->timeout, setup_timed_out);
  btstack_run_loop_set_timer_context(&remote->timeout, remote);
  set_timeout(remote, SETUP_TIMEOUT_MS);
  return remote;
}

static void send_reports(WiiRemote *remote) {
  while (remote->count && remote->interrupt_open && l2cap_can_send_packet_now(remote->interrupt)) {
    SessionMessage *report = &remote->queue[remote->head];
    uint8_t frame[1 + sizeof report->data] = {HID_OUTPUT};
    memcpy(frame + 1, report->data, report->size);
    if (l2cap_send(remote->interrupt, frame, 1 + report->size) != ERROR_CODE_SUCCESS) break;
    remote->head = (remote->head + 1) % OUTPUT_QUEUE;
    remote->count--;
  }
  if (remote->count && remote->interrupt_open) l2cap_request_can_send_now_event(remote->interrupt);
}

static void queue_report(WiiRemote *remote, const uint8_t *data, unsigned size) {
  if (remote->count == OUTPUT_QUEUE) {
    fprintf(stderr, "usb-wii: output queue full for Player %d\n", remote->player + 1);
    return;
  }
  SessionMessage *report = &remote->queue[(remote->head + remote->count++) % OUTPUT_QUEUE];
  report->size = size;
  memcpy(report->data, data, size);
  send_reports(remote);
}

static void ready_remote(WiiRemote *remote) {
  for (int player = 0; player < REMOTE_PLAYERS; player++) {
    if (by_player(player)) continue;
    remote->player = player;
    btstack_run_loop_remove_timer(&remote->timeout);
    session_send_player(usb.socket, REMOTE_CONNECTED, player, remote->address, sizeof remote->address);
    fprintf(stderr, "usb-wii: Wii Remote connected as Player %d\n", player + 1);
    rescan();
    return;
  }
}

static void channels(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
  if (type == L2CAP_DATA_PACKET) {
    WiiRemote *remote = by_channel(channel);
    if (!remote || channel != remote->interrupt || size < 2 || packet[0] != HID_INPUT) return;
    if (remote->player < 0 && remote->control_open && remote->interrupt_open &&
        size >= 7 && packet[1] == INPUT_STATUS) ready_remote(remote);
    if (remote->player >= 0)
      session_send_player(usb.socket, REMOTE_INPUT, remote->player, packet + 1, size - 1);
    return;
  }
  if (type != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(packet)) {
    case L2CAP_EVENT_INCOMING_CONNECTION: {
      bd_addr_t address;
      l2cap_event_incoming_connection_get_address(packet, address);
      uint16_t cid = l2cap_event_incoming_connection_get_local_cid(packet);
      uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
      if (usb.trace) fprintf(stderr, "usb-wii: incoming HID channel (PSM 0x%04x)\n", psm);
      WiiRemote *remote = by_address(address);
      if (!usb.stopping && !remote && has_bond(address)) remote = new_peer(address);
      if (usb.stopping || !remote) {
        l2cap_decline_connection(cid);
        return;
      }
      uint16_t *channel_cid = psm == PSM_HID_CONTROL ? &remote->control : &remote->interrupt;
      if (*channel_cid) {
        l2cap_decline_connection(cid);
        return;
      }
      remote->handle = l2cap_event_incoming_connection_get_handle(packet);
      *channel_cid = cid;
      l2cap_accept_connection(cid);
      break;
    }
    case L2CAP_EVENT_CHANNEL_OPENED: {
      uint16_t cid = l2cap_event_channel_opened_get_local_cid(packet);
      WiiRemote *remote = by_channel(cid);
      if (!remote) break;
      hci_con_handle_t handle = l2cap_event_channel_opened_get_handle(packet);
      if (handle != HCI_CON_HANDLE_INVALID) remote->handle = handle;
      uint8_t status = l2cap_event_channel_opened_get_status(packet);
      if (status) {
        fprintf(stderr, "usb-wii: HID channel failed (0x%02x). Press SYNC to pair again\n", status);
        disconnect_remote(remote);
        break;
      }
      if (cid == remote->control) {
        remote->control_open = 1;
        if (remote->outgoing && !remote->interrupt &&
            l2cap_create_channel(channels, remote->address, PSM_HID_INTERRUPT, REPORT_MTU, &remote->interrupt)) {
          disconnect_remote(remote);
          break;
        }
      } else remote->interrupt_open = 1;
      if (remote->control_open && remote->interrupt_open && remote->player < 0) {
        const uint8_t status_request[] = {REQUEST_STATUS, 0};
        set_timeout(remote, STATUS_TIMEOUT_MS);
        queue_report(remote, status_request, sizeof status_request);
      }
      break;
    }
    case L2CAP_EVENT_CHANNEL_CLOSED: {
      WiiRemote *remote = by_channel(l2cap_event_channel_closed_get_local_cid(packet));
      if (remote) disconnect_remote(remote);
      break;
    }
    case L2CAP_EVENT_CAN_SEND_NOW: {
      WiiRemote *remote = by_channel(l2cap_event_can_send_now_get_local_cid(packet));
      if (remote) send_reports(remote);
      break;
    }
  }
}

static void dial(const bd_addr_t address, int alternate_pin) {
  WiiRemote *remote = by_address(address);
  if (!remote) remote = new_peer(address);
  if (!remote || remote->control || remote->interrupt) return;
  remote->outgoing = 1;
  remote->alternate_pin = alternate_pin;
  fprintf(stderr, "usb-wii: opening Wii Remote HID channels%s\n", alternate_pin ? " with alternate PIN" : "");
  if (l2cap_create_channel(channels, remote->address, PSM_HID_CONTROL, REPORT_MTU, &remote->control))
    disconnect_remote(remote);
}

static int accept_connection(bd_addr_t address, hci_link_type_t type) {
  return !usb.stopping && type == HCI_LINK_TYPE_ACL &&
         (by_address(address) || (free_peer() && has_bond(address)));
}

static void controller_event(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
  (void)channel;
  if (type != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
      if (btstack_event_state_get_state(packet) == HCI_STATE_OFF) {
        if (usb.working || usb.stopping) btstack_run_loop_trigger_exit();
      } else if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING && !usb.working) {
        usb.working = 1;
        if (!open_keys()) {
          fprintf(stderr, "usb-wii: cannot open local pairing store\n");
          hci_power_control(HCI_POWER_OFF);
          return;
        }
        gap_connectable_control(1);
        fprintf(stderr, "usb-wii: Nintendo adapter ready. Wake a paired remote or press its red SYNC button\n");
        rescan();
      }
      break;
    case GAP_EVENT_INQUIRY_RESULT: {
      if (!can_discover() || usb.candidate_named) break;
      bd_addr_t address;
      gap_event_inquiry_result_get_bd_addr(packet, address);
      if (by_address(address)) break;
      uint32_t device_class = gap_event_inquiry_result_get_class_of_device(packet);
      int named = gap_event_inquiry_result_get_name_available(packet);
      const uint8_t *name = named ? gap_event_inquiry_result_get_name(packet) : (const uint8_t *)"";
      unsigned length = named ? gap_event_inquiry_result_get_name_len(packet) : 0;
      if (usb.trace)
        fprintf(stderr, "usb-wii: discovery class %06x, name %.*s\n", device_class, (int)length, name);
      uint32_t peripheral = device_class & PERIPHERAL_TYPE_MASK;
      int plausible = named ? is_remote(name, length, device_class)
                            : peripheral == (WII_REMOTE_CLASS & PERIPHERAL_TYPE_MASK) ||
                              peripheral == WII_REMOTE_PLUS_CLASS;
      if (!plausible || (address_set(usb.candidate) && !named)) break;
      memcpy(usb.candidate, address, sizeof address);
      usb.candidate_class = device_class;
      usb.clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
      usb.page_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
      usb.candidate_named = named;
      if (named) gap_inquiry_stop();
      break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
      usb.inquiry = 0;
      if (!can_discover()) break;
      if (usb.candidate_named) dial(usb.candidate, 0);
      else if (address_set(usb.candidate)) {
        usb.naming = gap_remote_name_request(usb.candidate, usb.page_mode, usb.clock_offset | 0x8000)
                     == ERROR_CODE_SUCCESS;
        if (!usb.naming) rescan();
      } else {
        // Some third-party remotes advertise only in a general inquiry.
        usb.general_inquiry = !usb.general_inquiry;
        rescan();
      }
      break;
    case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE: {
      bd_addr_t address;
      hci_event_remote_name_request_complete_get_bd_addr(packet, address);
      if (!usb.naming || memcmp(address, usb.candidate, sizeof address)) break;
      usb.naming = 0;
      int named = size > 9 && !hci_event_remote_name_request_complete_get_status(packet);
      if (usb.trace && named) fprintf(stderr, "usb-wii: remote name %.*s\n", size - 9,
                                      hci_event_remote_name_request_complete_get_remote_name(packet));
      if (can_discover() && named &&
          is_remote(hci_event_remote_name_request_complete_get_remote_name(packet), size - 9, usb.candidate_class))
        dial(address, 0);
      else rescan();
      break;
    }
    case HCI_EVENT_CONNECTION_REQUEST: {
      bd_addr_t address;
      hci_event_connection_request_get_bd_addr(packet, address);
      if (hci_event_connection_request_get_link_type(packet) == HCI_LINK_TYPE_ACL && has_bond(address)) {
        // Leave room for the saved remote's incoming ACL and HID setup.
        rescan();
        if (usb.inquiry && !address_set(usb.candidate)) gap_inquiry_stop();
      }
      break;
    }
    case HCI_EVENT_CONNECTION_COMPLETE: {
      uint8_t status = hci_event_connection_complete_get_status(packet);
      if (usb.trace) fprintf(stderr, "usb-wii: ACL connection complete (0x%02x)\n", status);
      if (status || usb.stopping || hci_event_connection_complete_get_link_type(packet) != HCI_LINK_TYPE_ACL) break;
      bd_addr_t address;
      hci_event_connection_complete_get_bd_addr(packet, address);
      WiiRemote *remote = by_address(address);
      hci_con_handle_t handle = hci_event_connection_complete_get_connection_handle(packet);
      if (!remote && has_bond(address)) remote = new_peer(address);
      if (!remote) {
        gap_disconnect(handle);
        break;
      }
      remote->handle = handle;
      if (!remote->outgoing) gap_request_security_level(handle, LEVEL_2);
      break;
    }
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
      hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
      if (usb.trace) fprintf(stderr, "usb-wii: ACL disconnected (0x%02x)\n",
                             hci_event_disconnection_complete_get_reason(packet));
      for (unsigned i = 0; i < REMOTE_PLAYERS; i++) {
        WiiRemote *remote = &usb.remotes[i];
        if (address_set(remote->address) && remote->handle == handle) lost(remote);
      }
      break;
    }
    case GAP_EVENT_PAIRING_COMPLETE: {
      bd_addr_t address;
      gap_event_pairing_complete_get_bd_addr(packet, address);
      WiiRemote *remote = by_address(address);
      uint8_t status = gap_event_pairing_complete_get_status(packet);
      if (usb.trace && remote) fprintf(stderr, "usb-wii: pairing finished (0x%02x)\n", status);
      // Some Wii-compatible remotes use a fixed PIN. Try it once after an explicit pairing fails.
      if (remote && remote->outgoing && remote->pin_requested && !remote->alternate_pin &&
          (status == ERROR_CODE_AUTHENTICATION_FAILURE || status == ERROR_CODE_PIN_OR_KEY_MISSING)) {
        memcpy(usb.retry_address, address, sizeof address);
        disconnect_remote(remote);
      }
      break;
    }
    case HCI_EVENT_PIN_CODE_REQUEST: {
      bd_addr_t address;
      hci_event_pin_code_request_get_bd_addr(packet, address);
      WiiRemote *remote = by_address(address);
      if (!remote) gap_pin_code_negative(address);
      else {
        remote->pin_requested = 1;
        if (remote->alternate_pin) gap_pin_code_response(address, "0000");
        else gap_pin_code_response_binary(address, usb.pin, sizeof usb.pin);
      }
      break;
    }
  }
}

static void browser_messages(btstack_data_source_t *source, btstack_data_source_callback_type_t kind) {
  (void)kind;
  SessionMessage report;
  int read;
  while ((read = session_read(usb.socket, &report, MSG_DONTWAIT)) > 0) {
    if (report.type == USB_DISCOVERY && report.size == 1) {
      usb.discovery = report.data[0] != 0;
      if (!usb.discovery && usb.inquiry) gap_inquiry_stop();
      rescan();
    } else if (report.type == REMOTE_OUTPUT && report.size) {
      WiiRemote *remote = by_player(report.player);
      if (remote) queue_report(remote, report.data, report.size);
    }
  }
  if (read == 0) {
    usb.stopping = 1;
    btstack_run_loop_remove_data_source(source);
    btstack_run_loop_remove_timer(&usb.scan_timer);
    hci_power_control(HCI_POWER_OFF);
  }
}

static void *run_usb(void *argument) {
  (void)argument;
  if (select_module()) {
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());
    hci_init(hci_transport_usb_instance(), NULL);
    // The Wii console becomes the central when a saved remote wakes.
    hci_set_master_slave_policy(HCI_ROLE_MASTER);
    l2cap_init();
    gap_set_bondable_mode(1);
    gap_register_classic_connection_filter(accept_connection);
    gap_ssp_set_enable(0);
    gap_set_security_level(LEVEL_2);
    l2cap_register_service(channels, PSM_HID_CONTROL, REPORT_MTU, LEVEL_2);
    l2cap_register_service(channels, PSM_HID_INTERRUPT, REPORT_MTU, LEVEL_2);
    static btstack_packet_callback_registration_t events;
    events.callback = controller_event;
    hci_add_event_handler(&events);
    static btstack_data_source_t source;
    btstack_run_loop_set_data_source_fd(&source, usb.socket);
    btstack_run_loop_set_data_source_handler(&source, browser_messages);
    btstack_run_loop_enable_data_source_callbacks(&source, DATA_SOURCE_CALLBACK_READ);
    btstack_run_loop_add_data_source(&source);
    btstack_run_loop_set_timer_handler(&usb.scan_timer, scan);
    if (hci_power_control(HCI_POWER_ON) == 0) btstack_run_loop_execute();
    else fprintf(stderr, "usb-wii: cannot claim Nintendo adapter. Close Dolphin and check USB access\n");
    hci_close();
    if (usb.tlv) btstack_tlv_posix_deinit(&usb.store);
  }
  close(usb.socket);
  return NULL;
}

int usb_remote_start(pthread_t *thread) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets)) return -1;
  usb.socket = sockets[1];
  usb.discovery = 1;
  usb.trace = getenv("JUST_DANCE_USB_TRACE") != NULL;
  int error = pthread_create(thread, NULL, run_usb, NULL);
  if (!error) return sockets[0];
  close(sockets[0]);
  close(sockets[1]);
  errno = error;
  return -1;
}

void usb_remote_stop(int fd, pthread_t thread) {
  shutdown(fd, SHUT_WR);
  pthread_join(thread, NULL);
  close(fd);
}
