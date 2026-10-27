#ifndef AIMBUDDY_CORE_MODEL_PROFILE_H
#define AIMBUDDY_CORE_MODEL_PROFILE_H

#include <cstring>
#include <string>

namespace aimbuddy {

/// Most classes a model may declare.
constexpr int kMaxModelClasses = 16;

/// What a detected class means to the app.
enum class ClassRole : unsigned char {
    kIgnore = 0,  ///< Detected but never highlighted or aimed at (teammates, the player's own character).
    kBody = 1,    ///< An enemy, or the visible part of one: the box that is outlined and tracked.
    kHead = 2,    ///< An enemy's head; when present it is preferred as the aim point.
};

/// How the detector's output tensor is arranged.
enum class OutputLayout : unsigned char {
    kChannelsFirst,  ///< (4 + classes) rows by anchors columns: the raw Ultralytics export.
    kRowsFirst,      ///< Anchors rows by (4 + classes) columns: the same data transposed.
    kEndToEnd,       ///< N rows of [x1, y1, x2, y2, score, class]: an export with built-in NMS.
};

/**
 * Tells the app how to read the model the user trained: what each class means and how the
 * output tensor is laid out. Models differ between users, so none of this is assumed from the
 * file alone; it comes from a small description saved next to the model (see parseRoles()).
 */
struct ModelProfile {
    ClassRole roles[kMaxModelClasses] = {};
    int declaredClassCount = 0;               ///< Classes the description lists; 0 means unknown.
    bool endToEndOutput = false;              ///< The model emits final detections (no NMS needed).
    int inputSize = 0;                        ///< Width and height of the network input; 0 means work it out.

    /// Role of a class id; classes the description does not list are ignored.
    ClassRole roleOf(int classId) const {
        return (classId >= 0 && classId < kMaxModelClasses) ? roles[classId] : ClassRole::kIgnore;
    }

    /// True when the model has a class dedicated to heads.
    bool hasHeadClass() const {
        for (int i = 0; i < kMaxModelClasses; ++i) {
            if (roles[i] == ClassRole::kHead) {
                return true;
            }
        }
        return false;
    }

    /**
     * Roles to assume when no description is saved: one class is the enemy body; two classes are
     * body then head (the layout the training scripts produce); anything further is ignored.
     */
    static ModelProfile defaultsFor(int classCount) {
        ModelProfile profile;
        profile.declaredClassCount = classCount;
        if (classCount >= 1) {
            profile.roles[0] = ClassRole::kBody;
        }
        if (classCount >= 2) {
            profile.roles[1] = ClassRole::kHead;
        }
        return profile;
    }

    /**
     * Reads roles from a comma separated list such as "body,head,ignore", one entry per class
     * in class order. Unknown words count as "ignore". An empty list gives defaultsFor(classCount).
     */
    static ModelProfile parseRoles(const std::string& list, int classCount) {
        if (list.empty()) {
            return defaultsFor(classCount);
        }
        ModelProfile profile;
        int index = 0;
        size_t start = 0;
        while (start <= list.size() && index < kMaxModelClasses) {
            size_t end = list.find(',', start);
            if (end == std::string::npos) {
                end = list.size();
            }
            std::string word = list.substr(start, end - start);
            const size_t first = word.find_first_not_of(" \t");
            const size_t last = word.find_last_not_of(" \t");
            word = first == std::string::npos ? "" : word.substr(first, last - first + 1);
            if (word == "body") {
                profile.roles[index] = ClassRole::kBody;
            } else if (word == "head") {
                profile.roles[index] = ClassRole::kHead;
            }
            ++index;
            start = end + 1;
        }
        profile.declaredClassCount = index;
        return profile;
    }
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_MODEL_PROFILE_H
