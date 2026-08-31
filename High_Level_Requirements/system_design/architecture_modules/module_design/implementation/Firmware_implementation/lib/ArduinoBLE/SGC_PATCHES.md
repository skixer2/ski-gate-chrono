# SGC-patched ArduinoBLE (vendored from upstream 2.0.2)

Patched for Ski Gate Chrono (FW 5.61) to fix unbounded busy-poll in the ACL
TX path that caused device WDT reboots (5.59 bench) and zombie hangs (5.60
bench) when the Samsung S22 BLE controller stalls during file transfer.

Upstream: https://github.com/arduino-libraries/ArduinoBLE @ 2.0.2

## Patches (all marked `// SGC PATCH`)

1. **`src/utility/HCI.cpp` — `HCIClass::sendAclPkt()`**: the busy-wait
   `while (_pendingPkt >= _maxPkt) poll();` is now bounded (2000 ms) and
   returns `-1` on timeout instead of spinning forever. The original code
   could freeze the main loop indefinitely (no WDT feed → watchdog reboot;
   with WDT survived → zombie hang, since no error ever propagated).

2. **`src/utility/HCI.cpp` — `HCIClass::handleEventPkt()` /
   `EVT_DISCONN_COMPLETE`**: `_pendingPkt = 0` added. On disconnect the
   controller flushes its TX queue, but the host-side counter was never
   cleared — so the sendAclPkt busy-poll never exited after the phone
   dropped the link.

3. **`src/utility/ATT.cpp` — `ATTClass::handleNotify()`**: the return value
   of `HCI.sendAclPkt()` is no longer discarded. A failed send makes
   handleNotify return 0 → `BLECharacteristic.writeValue()` returns 0 →
   the caller (SGC file_transfer.cpp) can detect and abort cleanly.
