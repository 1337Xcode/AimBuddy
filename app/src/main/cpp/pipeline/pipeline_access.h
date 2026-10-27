#ifndef AIMBUDDY_PIPELINE_PIPELINE_ACCESS_H
#define AIMBUDDY_PIPELINE_PIPELINE_ACCESS_H

#include "detection_pipeline.h"

namespace aimbuddy {

/**
 * The app's one detection pipeline.
 *
 * The overlay and the aim controller live in other parts of the program from the code that
 * creates the pipeline, so they reach it through this function instead of each holding a
 * pointer. It is defined next to the code that owns it (esp_jni.cpp).
 */
DetectionPipeline& pipeline();

}  // namespace aimbuddy

#endif  // AIMBUDDY_PIPELINE_PIPELINE_ACCESS_H
