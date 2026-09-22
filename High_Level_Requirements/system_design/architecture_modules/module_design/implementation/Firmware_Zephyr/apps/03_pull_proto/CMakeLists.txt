# Step 3c app: pull-protocol product app (modular)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pull_proto)

target_sources(app PRIVATE
  src/main.c
  src/store_fake.c
  src/led_state.c
  src/ble_lifecycle.c
  src/pull_proto.c
  src/demo_svc.c
)
