cmake_minimum_required(VERSION 3.16)
project(couch_btstack C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(USB REQUIRED IMPORTED_TARGET libusb-1.0)
set(BT "${BTSTACK_SOURCE}")
add_library(couch-btstack STATIC
    ${BT}/src/ad_parser.c
    ${BT}/src/btstack_linked_list.c
    ${BT}/src/btstack_memory.c
    ${BT}/src/btstack_memory_pool.c
    ${BT}/src/btstack_ring_buffer.c
    ${BT}/src/btstack_run_loop.c
    ${BT}/src/btstack_tlv.c
    ${BT}/src/btstack_util.c
    ${BT}/src/hci.c
    ${BT}/src/hci_cmd.c
    ${BT}/src/hci_dump.c
    ${BT}/src/l2cap.c
    ${BT}/src/l2cap_signaling.c
    ${BT}/src/classic/btstack_link_key_db_tlv.c
    ${BT}/platform/libusb/hci_transport_h2_libusb.c
    ${BT}/platform/posix/btstack_run_loop_posix.c
    ${BT}/platform/posix/btstack_tlv_posix.c
)
target_include_directories(couch-btstack PRIVATE . ${BT}/src ${BT}/platform/posix)
target_link_libraries(couch-btstack PRIVATE PkgConfig::USB)
