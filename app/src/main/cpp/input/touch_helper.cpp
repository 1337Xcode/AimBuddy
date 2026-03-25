/**
 * touch_helper.cpp - uinput-based touch simulation
 *
 * Creates a virtual touchscreen and forwards touch events
 * through Linux uinput using slot-based contact state.
 */

#include "touch_helper.h"

#include <android/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define LOG_TAG "TouchHelper"
#if defined(NDEBUG)
    #define LOGE(...) ((void)0)
#else
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

namespace {
constexpr int kFakeContact = 9;

struct TouchContact {
    int posX = -1;
    int posY = -1;
    bool enabled = false;
};

struct InputDevice {
    int ifd = -1;
    int slots = 0;
    int version = 0;
    int touchXMin = 0;
    int touchXMax = 0;
    int touchYMin = 0;
    int touchYMax = 0;
    bool isGrabbed = false;
    bool hasPressure = false;
    bool hasTouchMajor = false;
    bool hasTouchMinor = false;
    bool hasWidthMajor = false;
    bool hasWidthMinor = false;
    bool hasOrientation = false;
    struct input_id iid{};
    unsigned char evbits[EV_MAX / 8 + 1] = {0};
    unsigned char absBits[ABS_MAX / 8 + 1] = {0};
    unsigned char keyBits[KEY_MAX / 8 + 1] = {0};
    unsigned char propBits[INPUT_PROP_MAX / 8 + 1] = {0};
    std::string name;
    std::string path;
    std::string phys;
    std::vector<struct input_absinfo> absInfos;
};

const char kLetterBytes[] = "abcdefghijklmnopqrstuvwxyz";

int g_displayWidth = 0;
int g_displayHeight = 0;

int g_uInputTouchFd = -1;
bool g_isBtnDown = false;
bool g_isStopped = false;
bool g_touchSend = false;
bool g_touchStart = false;
std::mutex g_touchSynMtx;
InputDevice g_touchDevice{};
std::vector<TouchContact> g_contacts;

int getFakeContactSlot() {
    if (g_touchDevice.slots <= 0) {
        return 0;
    }
    return std::min(kFakeContact, g_touchDevice.slots - 1);
}

std::string randString(int n) {
    std::string b;
    b.resize(n);
    for (int i = 0; i < n; i++) {
        b[i] = kLetterBytes[rand() % (sizeof(kLetterBytes) - 1)];
    }
    return b;
}

int isCharDevice(std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == -1) {
        return 0;
    }
    return S_ISCHR(st.st_mode) ? 1 : 0;
}

bool hasSpecificType(InputDevice& id, unsigned int prop) {
    return id.evbits[prop / 8] & (1 << (prop % 8));
}

bool hasSpecificProp(InputDevice& id, unsigned int prop) {
    return id.propBits[prop / 8] & (1 << (prop % 8));
}

bool hasSpecificAbs(InputDevice& id, unsigned int key) {
    return id.absBits[key / 8] & (1 << (key % 8));
}

bool hasSpecificKey(InputDevice& id, unsigned int key) {
    return id.keyBits[key / 8] & (1 << (key % 8));
}

struct input_absinfo getAbsInfo(int ifd, unsigned int key) {
    struct input_absinfo absinfo{};
    ioctl(ifd, EVIOCGABS(key), &absinfo);
    return absinfo;
}

struct input_id getInputID(int ifd) {
    struct input_id iid{};
    ioctl(ifd, EVIOCGID, &iid);
    return iid;
}

std::string getDeviceName(int ifd) {
    std::string name;
    name.resize(UINPUT_MAX_NAME_SIZE);
    ioctl(ifd, EVIOCGNAME(UINPUT_MAX_NAME_SIZE), name.data());
    return name;
}

std::string getPhysLoc(int ifd) {
    std::string phys;
    phys.resize(UINPUT_MAX_NAME_SIZE);
    ioctl(ifd, EVIOCGPHYS(UINPUT_MAX_NAME_SIZE), phys.data());
    return phys;
}

std::vector<InputDevice> getTouchDevice() {
    struct dirent* entry;
    std::string input_path = "/dev/input";

    DIR* dir = opendir(input_path.c_str());
    if (!dir) {
        return {};
    }

    std::vector<InputDevice> devs;

    while ((entry = readdir(dir))) {
        if (!strstr(entry->d_name, "event")) {
            continue;
        }

        std::string devname = input_path + "/" + entry->d_name;

        if (!isCharDevice(devname)) {
            continue;
        }

        int ifd = open(devname.c_str(), O_RDONLY);
        if (ifd < 0) {
            continue;
        }

        InputDevice id{};

        if (ioctl(ifd, EVIOCGBIT(0, sizeof(id.evbits)), &id.evbits) < 0) {
            close(ifd);
            continue;
        }

        if (ioctl(ifd, EVIOCGBIT(EV_ABS, sizeof(id.absBits)), &id.absBits) < 0) {
            close(ifd);
            continue;
        }

        if (ioctl(ifd, EVIOCGPROP(sizeof(id.propBits)), &id.propBits) < 0) {
            close(ifd);
            continue;
        }

        if (ioctl(ifd, EVIOCGBIT(EV_KEY, sizeof(id.keyBits)), &id.keyBits) < 0) {
            close(ifd);
            continue;
        }

        if (hasSpecificAbs(id, ABS_MT_SLOT - 1) || !hasSpecificAbs(id, ABS_MT_SLOT) ||
            !hasSpecificAbs(id, ABS_MT_TRACKING_ID) || !hasSpecificAbs(id, ABS_MT_POSITION_X) ||
            !hasSpecificAbs(id, ABS_MT_POSITION_Y) || !hasSpecificProp(id, INPUT_PROP_DIRECT) ||
            !hasSpecificKey(id, BTN_TOUCH)) {
            close(ifd);
            continue;
        }

        id.absInfos.resize(ABS_CNT);
        for (unsigned int i = 0; i < ABS_CNT; i++) {
            if (!hasSpecificAbs(id, i)) {
                continue;
            }

            auto absInfo = getAbsInfo(ifd, i);

            switch (i) {
                case ABS_MT_SLOT:
                    id.slots = absInfo.maximum + 1;
                    break;
                case ABS_MT_TRACKING_ID:
                    if (absInfo.minimum == absInfo.maximum) {
                        absInfo.minimum = -1;
                        absInfo.maximum = 0xFFFF;
                    }
                    break;
                case ABS_MT_POSITION_X:
                    id.touchXMin = absInfo.minimum;
                    id.touchXMax = absInfo.maximum - absInfo.minimum + 1;
                    break;
                case ABS_MT_POSITION_Y:
                    id.touchYMin = absInfo.minimum;
                    id.touchYMax = absInfo.maximum - absInfo.minimum + 1;
                    break;
                default:
                    break;
            }

            id.absInfos[i] = absInfo;
        }

        id.iid = getInputID(ifd);

        if (ioctl(ifd, EVIOCGVERSION, &id.version) < 0) {
            close(ifd);
            continue;
        }

        id.path = devname;
        id.phys = getPhysLoc(ifd);
        id.name = getDeviceName(ifd);
        id.hasTouchMajor = hasSpecificAbs(id, ABS_MT_TOUCH_MAJOR);
        id.hasTouchMinor = hasSpecificAbs(id, ABS_MT_TOUCH_MINOR);
        id.hasWidthMajor = hasSpecificAbs(id, ABS_MT_WIDTH_MAJOR);
        id.hasWidthMinor = hasSpecificAbs(id, ABS_MT_WIDTH_MINOR);
        id.hasOrientation = hasSpecificAbs(id, ABS_MT_ORIENTATION);
        id.hasPressure = hasSpecificAbs(id, ABS_MT_PRESSURE);

        devs.push_back(id);

        close(ifd);
    }

    closedir(dir);

    return devs;
}

int createUInput(InputDevice& dev) {
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        LOGE("Unable to open uinput");
        return -1;
    }

    dev.ifd = open(dev.path.c_str(), O_RDONLY);
    if (dev.ifd < 0) {
        LOGE("Unable to open touch device");
        close(ufd);
        return -1;
    }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    for (unsigned int key = 0; key < KEY_CNT; key++) {
        if (hasSpecificKey(dev, key)) {
            ioctl(ufd, UI_SET_KEYBIT, key);
        }
    }

    struct uinput_user_dev uidev{};
    memset(&uidev, 0, sizeof(uidev));

    std::string devName = dev.name;
    std::string randStr = "_" + randString(4);
    strncat((char*)devName.data(), randStr.data(), randStr.size());

    strncpy(uidev.name, devName.data(), UINPUT_MAX_NAME_SIZE);

    uidev.id.bustype = dev.iid.bustype;
    uidev.id.vendor = dev.iid.version;
    uidev.id.product = dev.iid.product;
    uidev.id.version = dev.iid.version;

    uidev.ff_effects_max = 0;
    if (hasSpecificType(dev, EV_FF)) {
        uidev.ff_effects_max = 10;
    }

    ioctl(ufd, UI_SET_EVBIT, EV_ABS);

    for (unsigned int abs = 0; abs < ABS_CNT; abs++) {
        if (!hasSpecificAbs(dev, abs)) {
            continue;
        }

        if (abs == ABS_MT_POSITION_X || abs == ABS_MT_POSITION_Y || abs == ABS_MT_TRACKING_ID) {
            ioctl(ufd, UI_SET_ABSBIT, abs);
            uidev.absmin[abs] = dev.absInfos[abs].minimum;
            uidev.absmax[abs] = dev.absInfos[abs].maximum;
            uidev.absfuzz[abs] = dev.absInfos[abs].fuzz;
            uidev.absflat[abs] = dev.absInfos[abs].flat;
        }
    }

    for (unsigned int prop = 0; prop < INPUT_PROP_CNT; prop++) {
        if (hasSpecificProp(dev, prop)) {
            ioctl(ufd, UI_SET_PROPBIT, prop);
        }
    }

    ioctl(ufd, UI_SET_PHYS, dev.phys.c_str());

    write(ufd, &uidev, sizeof(uidev));

    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        LOGE("Unable to create uinput");
        close(ufd);
        close(dev.ifd);
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ioctl(dev.ifd, EVIOCGRAB, 1);

    return ufd;
}

void writeEvent(int ifd, int type, int code, int value) {
    struct input_event inputEvent = {};
    inputEvent.type = type;
    inputEvent.code = code;
    inputEvent.value = value;
    write(ifd, &inputEvent, sizeof(input_event));
}

void eventUpdate() {
    if (g_isStopped) {
        return;
    }

    int nextSlot = 0;

    for (int i = 0; i < g_touchDevice.slots; i++) {
        if (g_contacts[i].enabled && g_contacts[i].posX > 0 && g_contacts[i].posY > 0) {
            writeEvent(g_uInputTouchFd, EV_ABS, ABS_MT_POSITION_X, g_contacts[i].posX);
            writeEvent(g_uInputTouchFd, EV_ABS, ABS_MT_POSITION_Y, g_contacts[i].posY);
            writeEvent(g_uInputTouchFd, EV_ABS, ABS_MT_TRACKING_ID, i);
            writeEvent(g_uInputTouchFd, EV_SYN, SYN_MT_REPORT, 0x0);
            nextSlot++;
        }
    }

    if (nextSlot == 0 && g_isBtnDown) {
        g_isBtnDown = false;
        writeEvent(g_uInputTouchFd, EV_SYN, SYN_MT_REPORT, 0x0);
        writeEvent(g_uInputTouchFd, EV_KEY, BTN_TOUCH, 0x0);
    } else if (nextSlot == 1 && !g_isBtnDown) {
        g_isBtnDown = true;
        writeEvent(g_uInputTouchFd, EV_KEY, BTN_TOUCH, 0x1);
    }

    writeEvent(g_uInputTouchFd, EV_SYN, SYN_REPORT, 0x0);
}

void eventReaderThread() {
    int currSlot = 0;
    bool hasSyn = false;
    struct input_event evt{};

    while (!g_isStopped && read(g_touchDevice.ifd, &evt, sizeof(evt))) {
        std::lock_guard<std::mutex> lock(g_touchSynMtx);

        switch (evt.type) {
            case EV_SYN:
                if (evt.code == SYN_REPORT) {
                    hasSyn = true;
                }
                break;
            case EV_KEY:
                if (evt.code == BTN_TOUCH) {
                }
                break;
            case EV_ABS:
                switch (evt.code) {
                    case ABS_MT_SLOT:
                        if (evt.value >= 0 && evt.value < g_touchDevice.slots) {
                            currSlot = evt.value;
                        }
                        break;
                    case ABS_MT_TRACKING_ID:
                        if (currSlot >= 0 && currSlot < static_cast<int>(g_contacts.size())) {
                            g_contacts[currSlot].enabled = evt.value != -1;
                        }
                        break;
                    case ABS_MT_POSITION_X:
                        if (currSlot >= 0 && currSlot < static_cast<int>(g_contacts.size())) {
                            g_contacts[currSlot].posX = evt.value;
                        }
                        break;
                    case ABS_MT_POSITION_Y:
                        if (currSlot >= 0 && currSlot < static_cast<int>(g_contacts.size())) {
                            g_contacts[currSlot].posY = evt.value;
                        }
                        break;
                }
                break;
        }

        if (hasSyn) {
            eventUpdate();
            hasSyn = false;
        }
    }
}

void sendTouchMove(int x, int y) {
    if (!g_touchStart) {
        return;
    }

    if (!g_touchSend) {
        g_touchSend = true;
    }

    std::lock_guard<std::mutex> lock(g_touchSynMtx);

    int finalX, finalY;
    // Hardcoded Landscape-on-Portrait Fix
    // Game is Landscape (1920x1080), Device is Portrait (1080x2400)
    // Game X (Long) -> Device Y (Long)
    // Game Y (Short) -> Device X (Short)
    
    bool rotate90 = true; 
    
    if (rotate90) {
        // X -> Y mapping: Scale Input X (0..GameW) to Device Y Range
        long gameX = x;
        long gameY = y;
        
        // Scale factors
        // Target Y = Game X * (DeviceH / GameW)
        long deviceY = gameX * (long)(g_touchDevice.touchYMax - g_touchDevice.touchYMin) / g_displayWidth;
        
        // Target X = Game Y * (DeviceW / GameH)
        long deviceX = gameY * (long)(g_touchDevice.touchXMax - g_touchDevice.touchXMin) / g_displayHeight;
        
        // Apply offset
        // FIXED: Yaw (Game X -> Device Y) is inverted. Flip it.
        // Screen Right -> Device Bottom (High Y) or Device Top (Low Y)?
        // User reported "Upper Left" for "Right Up".
        // "Upper" (Low X) is correct for Up.
        // "Left" (Low Y?) is wrong for Right.
        // So Right (High X) -> Low Y.
        // We want High Y.
        // Wait, if "Upper Left" means Low X, Low Y.
        // And we want "Upper Right".
        // "Upper" = Low X (Correct).
        // "Right" = High Y (if Device Y is Horizontal).
        // So we need to INVERT Y if it was Low.
        
        // Inverting Y mapping:
        finalY = (g_touchDevice.touchYMax - deviceY);
        finalX = deviceX + g_touchDevice.touchXMin; // Keep X (Pitch) as is (User said "Upper" works)
        
        // Clamp
        if (finalY < g_touchDevice.touchYMin) finalY = g_touchDevice.touchYMin;
        if (finalY > g_touchDevice.touchYMax) finalY = g_touchDevice.touchYMax;
    } else {
        // Standard Mapping
        finalX = (x * g_touchDevice.touchXMax / g_displayWidth) + g_touchDevice.touchXMin;
        finalY = (y * g_touchDevice.touchYMax / g_displayHeight) + g_touchDevice.touchYMin;
    }

    const int fakeContact = getFakeContactSlot();
    g_contacts[fakeContact].posX = finalX;
    g_contacts[fakeContact].posY = finalY;
    g_contacts[fakeContact].enabled = true;

    eventUpdate();
}

void sendTouchUp() {
    if (!g_touchStart || !g_touchSend) {
        return;
    }

    g_touchSend = false;

    std::lock_guard<std::mutex> lock(g_touchSynMtx);

    const int fakeContact = getFakeContactSlot();
    g_contacts[fakeContact].posX = -1;
    g_contacts[fakeContact].posY = -1;
    g_contacts[fakeContact].enabled = false;

    eventUpdate();
}

void resetTouch() {
    g_touchSend = false;

    {
        std::lock_guard<std::mutex> lock(g_touchSynMtx);
        for (int i = 0; i < g_touchDevice.slots; i++) {
            g_contacts[i].posX = -1;
            g_contacts[i].posY = -1;
            g_contacts[i].enabled = false;
        }
    }

    eventUpdate();
}

void updateRes(int x, int y) {
    g_displayWidth = x;
    g_displayHeight = y;
}

void touchInputStart() {
    if (!g_touchStart) {
        g_isStopped = false;
        g_touchSend = false;

        auto devs = getTouchDevice();
        if (devs.empty()) {
            LOGE("Devices are not found");
            return;
        }

        if (devs.size() > 1) {
            g_touchDevice = devs[1];
        } else {
            g_touchDevice = devs[0];
        }

        g_uInputTouchFd = createUInput(g_touchDevice);
        if (g_uInputTouchFd < 0) {
            LOGE("Unable to create virtual touch device");
            return;
        }

        g_contacts.clear();
        g_contacts.resize(g_touchDevice.slots);
        for (int i = 0; i < g_touchDevice.slots; i++) {
            g_contacts[i].posX = -1;
            g_contacts[i].posY = -1;
            g_contacts[i].enabled = false;
        }

        std::thread(eventReaderThread).detach();
        g_touchStart = true;
    }
}

void touchInputStop() {
    if (g_touchStart && g_touchDevice.ifd > -1 && g_uInputTouchFd > -1) {
        resetTouch();

        g_isStopped = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));

        close(g_touchDevice.ifd);
        close(g_uInputTouchFd);

        g_touchDevice.ifd = -1;
        g_uInputTouchFd = -1;

        g_touchStart = false;
    }
}

} // namespace

TouchHelper::TouchHelper() = default;

TouchHelper::~TouchHelper() {
    shutdown();
}

bool TouchHelper::init() {
    if (g_touchStart) {
        return true;
    }

    touchInputStart();
    return g_touchStart;
}

void TouchHelper::setScreenSize(int width, int height) {
    updateRes(width, height);
}

void TouchHelper::touchDown(int slot, float x, float y) {
    (void)slot;
    sendTouchMove(static_cast<int>(x), static_cast<int>(y));
}

void TouchHelper::touchMove(int slot, float x, float y) {
    (void)slot;
    sendTouchMove(static_cast<int>(x), static_cast<int>(y));
}

void TouchHelper::touchUp(int slot) {
    (void)slot;
    sendTouchUp();
}

void TouchHelper::shutdown() {
    touchInputStop();
}
