# Pre-candidate Lapis rollback

The native ARM64 experiment was a local source build, not an invocation of the Steam Deck installer. Removed its runnable workspace directory, direct serial probe scripts, temporary source/build/dependency extractions and test state. Preserved private local audit records separately; these are not part of this Git repository.

Verified the owned managed task stopped and its transient host service was inactive/not found with MainPID=0. No persistent SerialOSC service, new workspace package profile, or UDP 12002 listener was found. No recent change was found in the checked system udev, module or service configuration paths. Existing Lapis MTP/gadget/hardware/power services were untouched. No GitHub branches were modified as part of cleanup.

This is not a whole-system snapshot comparison or hardware-state restoration. Earlier query bytes and serial control-line/termios changes cannot be undone by removing files. No pre-test termios snapshot exists. The device remained disconnected during cleanup. Normal Arc startup has not been demonstrated since the anomaly, so no recovery or electrical-safety claim is made.

This new source candidate is intentionally separate: it preserves existing lease code and adds offline/passive integration tooling only. Cloning or compiling the offline target neither installs nor runs a daemon.
