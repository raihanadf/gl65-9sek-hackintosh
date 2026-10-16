// keeps the default output on the "GL65 Multi" device: when macOS switches it to the airpods
// (bluetooth), it is moved to the multi-output so music.app streams keep working.
// build: xcrun --sdk macosx clang -arch x86_64 -O2 -o airpods_multi airpods_multi.c -framework CoreAudio -framework CoreFoundation
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

static const AudioObjectPropertyAddress defaultOut = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

// moves the default output to the multi-output when the current default is a bluetooth device
static void check_default(void) {
    AudioDeviceID current = 0;
    UInt32 size = sizeof(current);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultOut, 0, NULL, &size, &current);

    UInt32 transport = 0;
    size = sizeof(transport);
    AudioObjectPropertyAddress taddr = {kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectGetPropertyData(current, &taddr, 0, NULL, &size, &transport);
    if (transport != kAudioDeviceTransportTypeBluetooth)
        return;

    AudioObjectPropertyAddress daddr = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioDeviceID ids[64];
    size = sizeof(ids);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &daddr, 0, NULL, &size, ids);

    for (UInt32 i = 0; i < size / sizeof(AudioDeviceID); i++) {
        CFStringRef name = NULL;
        UInt32 n = sizeof(name);
        AudioObjectPropertyAddress naddr = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        AudioObjectGetPropertyData(ids[i], &naddr, 0, NULL, &n, &name);
        if (!name)
            continue;
        Boolean match = CFStringCompare(name, CFSTR("GL65 Multi"), 0) == kCFCompareEqualTo;
        CFRelease(name);
        if (match) {
            OSStatus err = AudioObjectSetPropertyData(kAudioObjectSystemObject, &defaultOut, 0, NULL, sizeof(ids[i]), &ids[i]);
            printf("bluetooth output selected, switched to GL65 Multi: %d\n", (int)err);
            fflush(stdout);
            return;
        }
    }
    printf("bluetooth output selected, but GL65 Multi does not exist\n");
    fflush(stdout);
}

int main(void) {
    dispatch_queue_t queue = dispatch_get_main_queue();
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &defaultOut, queue,
                                        ^(UInt32 count, const AudioObjectPropertyAddress *addrs) { check_default(); });
    check_default();
    dispatch_main();
}
