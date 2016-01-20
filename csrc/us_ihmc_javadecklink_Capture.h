/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class us_ihmc_javadecklink_Capture */

#ifndef _Included_us_ihmc_javadecklink_Capture
#define _Included_us_ihmc_javadecklink_Capture
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     us_ihmc_javadecklink_Capture
 * Method:    getHardwareTime
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_javadecklink_Capture_getHardwareTime
  (JNIEnv *, jobject, jlong);

/*
 * Class:     us_ihmc_javadecklink_Capture
 * Method:    startCaptureNative
 * Signature: (Ljava/lang/String;ID)J
 */
JNIEXPORT jlong JNICALL Java_us_ihmc_javadecklink_Capture_startCaptureNative
  (JNIEnv *, jobject, jstring, jint, jint);

/*
 * Class:     us_ihmc_javadecklink_Capture
 * Method:    stopCaptureNative
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_us_ihmc_javadecklink_Capture_stopCaptureNative
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
