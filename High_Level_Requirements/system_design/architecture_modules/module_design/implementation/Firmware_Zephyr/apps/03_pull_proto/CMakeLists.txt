# Step 3a app: pull-protocol skeleton (product console service)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pull_proto)

target_sources(app PRIVATE src/main.c)
