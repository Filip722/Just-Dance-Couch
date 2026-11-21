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
  REMOTE_TAG = 0x57494931, REPORT_MTU = 64, OUTPUT_QUEUE = 32,
  INQUIRY_UNIT_MS = 1280, PAIR_INQUIRY_UNITS = 4,
  RETRY_MS = 1000, BONDED_LISTEN_MS = PAIR_INQUIRY_UNITS * INQUIRY_UNIT_MS,
};

/* All Bluetooth state is used by the BTstack thread only. */
static struct {
  int socket, working, stopping, inquiry, naming, connected, outgoing, trace;
  unsigned inquiry_results;
  uint16_t control, interrupt;
  int control_open, interrupt_open;
  bd_addr_t candidate, peer, bonded, pin;
  uint16_t clock_offset;
  uint8_t page_mode;
  int candidate_named;
  btstack_tlv_posix_t store;
  const btstack_tlv_t *tlv;
  char key_path[PATH_MAX];
  SessionMessage queue[OUTPUT_QUEUE];
  unsigned head, count;
  btstack_timer_source_t scan_timer;
} usb;

static int address_set(const bd_addr_t address) {
  static const bd_addr_t zero;
  return memcmp(address, zero, sizeof zero) != 0;
}

static int is_remote(const void *name, unsigned size) {
  static const char remote[] = "Nintendo RVL-CNT-01";
  static const char remote_plus[] = "Nintendo RVL-CNT-01-TR";
  size = strnlen(name, size);
  return (size == sizeof remote - 1 && !memcmp(name, remote, size)) ||
         (size == sizeof remote_plus - 1 && !memcmp(name, remote_plus, size));
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
  if (usb.tlv->get_tag(&usb.store, REMOTE_TAG, usb.bonded, sizeof usb.bonded) != sizeof usb.bonded)
    memset(usb.bonded, 0, sizeof usb.bonded);
  if (usb.trace && address_set(usb.bonded)) {
    link_key_t key;
    link_key_type_t type;
    int found = gap_get_link_key_for_bd_addr(usb.bonded, key, &type);
    explicit_bzero(key, sizeof key);
    fprintf(stderr, "usb-wii: saved Wii Remote address with %s\n", found ? "a link key" : "no link key");
  }
  return 1;
}

static void scan(btstack_timer_source_t *timer) {
  (void)timer;
  if (usb.stopping || usb.inquiry || usb.naming || address_set(usb.peer)) return;
  memset(usb.candidate, 0, sizeof usb.candidate);
  usb.candidate_named = 0;
  usb.inquiry_results = 0;
  int status = gap_inquiry_start(PAIR_INQUIRY_UNITS);
  usb.inquiry = status == ERROR_CODE_SUCCESS;
  if (usb.trace && status) fprintf(stderr, "usb-wii: inquiry start failed (0x%02x)\n", status);
  if (!usb.inquiry) {
    btstack_run_loop_set_timer(&usb.scan_timer, RETRY_MS);
    btstack_run_loop_add_timer(&usb.scan_timer);
  }
}

static void rescan(void) {
  btstack_run_loop_remove_timer(&usb.scan_timer);
  if (usb.stopping) return;
  btstack_run_loop_set_timer(&usb.scan_timer, address_set(usb.bonded) ? BONDED_LISTEN_MS : RETRY_MS);
  btstack_run_loop_add_timer(&usb.scan_timer);
}

static void lost(void) {
  if (usb.connected) {
    session_send(usb.socket, REMOTE_DISCONNECTED, NULL, 0);
    fprintf(stderr, "usb-wii: remote disconnected\n");
  }
  usb.connected = usb.control_open = usb.interrupt_open = usb.outgoing = 0;
  usb.control = usb.interrupt = 0;
  usb.head = usb.count = 0;
  memset(usb.peer, 0, sizeof usb.peer);
  rescan();
}

static void send_reports(void) {
  while (usb.count && usb.connected && l2cap_can_send_packet_now(usb.interrupt)) {
    SessionMessage *report = &usb.queue[usb.head];
    uint8_t frame[1 + sizeof report->data] = {HID_OUTPUT};
    memcpy(frame + 1, report->data, report->size);
    if (l2cap_send(usb.interrupt, frame, 1 + report->size) != ERROR_CODE_SUCCESS) break;
    usb.head = (usb.head + 1) % OUTPUT_QUEUE;
    usb.count--;
  }
  if (usb.count && usb.connected) l2cap_request_can_send_now_event(usb.interrupt);
}

static void channels(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
  if (type == L2CAP_DATA_PACKET && usb.connected && channel == usb.interrupt &&
      size > 1 && packet[0] == HID_INPUT) {
    session_send(usb.socket, REMOTE_INPUT, packet + 1, size - 1);
    return;
  }
  if (type != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(packet)) {
    case L2CAP_EVENT_INCOMING_CONNECTION: {
      bd_addr_t address;
      l2cap_event_incoming_connection_get_address(packet, address);
      if (usb.trace)
        fprintf(stderr, "usb-wii: incoming HID channel (PSM 0x%04x)\n",
                l2cap_event_incoming_connection_get_psm(packet));
      uint16_t cid = l2cap_event_incoming_connection_get_local_cid(packet);
      uint16_t *slot = l2cap_event_incoming_connection_get_psm(packet) == PSM_HID_CONTROL
                         ? &usb.control : &usb.interrupt;
      if (usb.stopping || !address_set(usb.bonded) || memcmp(address, usb.bonded, sizeof address) ||
          (address_set(usb.peer) && memcmp(address, usb.peer, sizeof address)) || *slot) {
        l2cap_decline_connection(cid);
        return;
      }
      memcpy(usb.peer, address, sizeof address);
      *slot = cid;
      l2cap_accept_connection(cid);
      break;
    }
    case L2CAP_EVENT_CHANNEL_OPENED: {
      uint16_t cid = l2cap_event_channel_opened_get_local_cid(packet);
      if (cid != usb.control && cid != usb.interrupt) break;
      hci_con_handle_t handle = l2cap_event_channel_opened_get_handle(packet);
      if (l2cap_event_channel_opened_get_status(packet)) {
        fprintf(stderr, "usb-wii: HID channel failed (0x%02x). Press SYNC to pair again\n",
                l2cap_event_channel_opened_get_status(packet));
        if (handle != HCI_CON_HANDLE_INVALID) gap_disconnect(handle);
        lost();
        break;
      }
      if (cid == usb.control) {
        usb.control_open = 1;
        if (usb.outgoing && !usb.interrupt &&
            l2cap_create_channel(channels, usb.peer, PSM_HID_INTERRUPT, REPORT_MTU, &usb.interrupt)) {
          gap_disconnect(handle);
          lost();
          break;
        }
      } else usb.interrupt_open = 1;
      if (!usb.connected && usb.control_open && usb.interrupt_open) {
        usb.connected = 1;
        memcpy(usb.bonded, usb.peer, sizeof usb.bonded);
        usb.tlv->store_tag(&usb.store, REMOTE_TAG, usb.bonded, sizeof usb.bonded);
        session_send(usb.socket, REMOTE_CONNECTED, usb.peer, sizeof usb.peer);
        fprintf(stderr, "usb-wii: Wii Remote connected\n");
      }
      break;
    }
    case L2CAP_EVENT_CHANNEL_CLOSED: {
      uint16_t cid = l2cap_event_channel_closed_get_local_cid(packet);
      if (cid == usb.control || cid == usb.interrupt) {
        uint16_t other = cid == usb.control ? usb.interrupt : usb.control;
        lost();
        if (other) l2cap_disconnect(other);
      }
      break;
    }
    case L2CAP_EVENT_CAN_SEND_NOW:
      if (l2cap_event_can_send_now_get_local_cid(packet) == usb.interrupt) send_reports();
      break;
  }
}

static void dial(void) {
  if (address_set(usb.peer)) return;
  memcpy(usb.peer, usb.candidate, sizeof usb.peer);
  usb.outgoing = 1;
  fprintf(stderr, "usb-wii: opening Wii Remote HID channels\n");
  if (l2cap_create_channel(channels, usb.peer, PSM_HID_CONTROL, REPORT_MTU, &usb.control)) lost();
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
      usb.inquiry_results++;
      if (address_set(usb.peer)) break;
      uint32_t device_class = gap_event_inquiry_result_get_class_of_device(packet);
      int named = gap_event_inquiry_result_get_name_available(packet);
      if (named && !is_remote(gap_event_inquiry_result_get_name(packet),
                              gap_event_inquiry_result_get_name_len(packet))) break;
      if (!named && device_class != WII_REMOTE_CLASS && device_class != WII_REMOTE_PLUS_CLASS) break;
      if (address_set(usb.candidate) && !named) break;
      gap_event_inquiry_result_get_bd_addr(packet, usb.candidate);
      usb.clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
      usb.page_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
      usb.candidate_named = named;
      fprintf(stderr, "usb-wii: found a Wii Remote candidate%s\n", named ? " by name" : " by device class");
      if (named) gap_inquiry_stop();
      break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
      if (usb.trace)
        fprintf(stderr, "usb-wii: limited inquiry finished (0x%02x, %u responses)\n",
                gap_event_inquiry_complete_get_status(packet), usb.inquiry_results);
      usb.inquiry = 0;
      if (usb.stopping || address_set(usb.peer)) break;
      if (usb.candidate_named) dial();
      else if (address_set(usb.candidate)) {
        usb.naming = gap_remote_name_request(usb.candidate, usb.page_mode, usb.clock_offset | 0x8000)
                     == ERROR_CODE_SUCCESS;
        if (!usb.naming) rescan();
      } else rescan();
      break;
    case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
      if (!usb.naming) break;
      usb.naming = 0;
      if (!usb.stopping && size > 9 && !hci_event_remote_name_request_complete_get_status(packet) &&
          is_remote(hci_event_remote_name_request_complete_get_remote_name(packet), size - 9)) dial();
      else {
        if (!usb.stopping) fprintf(stderr, "usb-wii: could not confirm a Wii Remote name\n");
        rescan();
      }
      break;
    case HCI_EVENT_CONNECTION_REQUEST: {
      bd_addr_t address;
      hci_event_connection_request_get_bd_addr(packet, address);
      if (hci_event_connection_request_get_link_type(packet) == HCI_LINK_TYPE_ACL &&
          address_set(usb.bonded) && !memcmp(address, usb.bonded, sizeof address)) {
        if (usb.trace) fprintf(stderr, "usb-wii: saved Wii Remote requested reconnection\n");
        // Give the incoming ACL and HID setup time before the next pairing inquiry.
        rescan();
        if (usb.inquiry && !address_set(usb.candidate)) gap_inquiry_stop();
      }
      break;
    }
    case HCI_EVENT_COMMAND_STATUS:
      if (usb.trace &&
          hci_event_command_status_get_command_opcode(packet) == HCI_OPCODE_HCI_ACCEPT_CONNECTION_REQUEST)
        fprintf(stderr, "usb-wii: incoming ACL accept (0x%02x)\n",
                hci_event_command_status_get_status(packet));
      break;
    case HCI_EVENT_CONNECTION_COMPLETE: {
      int status = hci_event_connection_complete_get_status(packet);
      if (usb.trace) fprintf(stderr, "usb-wii: ACL connection complete (0x%02x)\n", status);
      if (status || usb.stopping || usb.outgoing ||
          hci_event_connection_complete_get_link_type(packet) != HCI_LINK_TYPE_ACL) break;
      bd_addr_t address;
      hci_event_connection_complete_get_bd_addr(packet, address);
      if (address_set(usb.bonded) && !memcmp(address, usb.bonded, sizeof address)) {
        // Authenticate the saved remote before waiting for it to open HID.
        gap_request_security_level(hci_event_connection_complete_get_connection_handle(packet), LEVEL_2);
      }
      break;
    }
    case HCI_EVENT_ROLE_CHANGE:
      if (usb.trace)
        fprintf(stderr, "usb-wii: ACL role %u (0x%02x)\n",
                hci_event_role_change_get_role(packet), hci_event_role_change_get_status(packet));
      break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
      if (usb.trace)
        fprintf(stderr, "usb-wii: ACL disconnected (0x%02x)\n",
                hci_event_disconnection_complete_get_reason(packet));
      break;
    case HCI_EVENT_LINK_KEY_REQUEST:
      if (usb.trace) fprintf(stderr, "usb-wii: controller requested a stored link key\n");
      break;
    case GAP_EVENT_SECURITY_LEVEL:
      if (usb.trace)
        fprintf(stderr, "usb-wii: security level %u (0x%02x)\n",
                gap_event_security_level_get_security_level(packet), gap_event_security_level_get_status(packet));
      break;
    case GAP_EVENT_PAIRING_COMPLETE:
      if (usb.trace && address_set(usb.peer))
        fprintf(stderr, "usb-wii: pairing finished (0x%02x)\n", gap_event_pairing_complete_get_status(packet));
      break;
    case HCI_EVENT_PIN_CODE_REQUEST: {
      bd_addr_t address;
      hci_event_pin_code_request_get_bd_addr(packet, address);
      if (address_set(usb.peer) && memcmp(address, usb.peer, sizeof address) == 0)
        gap_pin_code_response_binary(address, usb.pin, sizeof usb.pin);
      else gap_pin_code_negative(address);
      break;
    }
  }
}

static void browser_messages(btstack_data_source_t *source, btstack_data_source_callback_type_t kind) {
  (void)kind;
  SessionMessage report;
  int read;
  while ((read = session_read(usb.socket, &report, MSG_DONTWAIT)) > 0) {
    if (report.type != REMOTE_OUTPUT || !report.size || !usb.connected) continue;
    if (usb.count == OUTPUT_QUEUE) {
      fprintf(stderr, "usb-wii: output queue full\n");
      break;
    }
    usb.queue[(usb.head + usb.count++) % OUTPUT_QUEUE] = report;
    send_reports();
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
    gap_ssp_set_enable(0);
    // The red SYNC button makes the remote answer limited Wii console inquiries.
    gap_inquiry_set_lap(GAP_IAC_LIMITED_INQUIRY);
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
