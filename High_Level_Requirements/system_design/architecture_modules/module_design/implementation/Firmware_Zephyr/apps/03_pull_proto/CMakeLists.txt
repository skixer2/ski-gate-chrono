# Step 3c app: pull-protocol product app (modular)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pull_proto)

target_sources(app PRIVATE
  src/main.c
  src/store_flash.c
  src/led_state.c
  src/ble_lifecycle.c
  src/pull_proto.c
  src/demo_svc.c
  src/recorder.c
  src/sensor_prod.c
  src/bitpacker.c
  src/bitpack_test.c
  src/bp_ref/bit_packer.cpp
  src/bp_ref/bp_ref_glue.cpp
  src/bhy2/bhy2.c
  src/bhy2/bhy2_hif.c
  src/bhy2/bhy2_parse.c
  src/bhy2/bhy2_zephyr.c
)
