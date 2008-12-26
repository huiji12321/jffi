#include <sys/types.h>
#include <stdlib.h>
#include <endian.h>
#include <ffi.h>
#include <jni.h>
#include "jffi.h"
#include "Exception.h"
#include "Function.h"
#include "Array.h"
#include "com_kenai_jffi_Foreign.h"

#if defined(__i386__)
#  define USE_RAW 1
#endif

#define PARAM_SIZE (8)
#define MAX_STACK_ARGS (8)

#ifdef USE_RAW
static inline void
invokeArray(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer, FFIValue* retval)
{
    Function* ctx = (Function *) (uintptr_t) ctxAddress;
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
    
    if (ctx->cif.nargs > 0) {
        if (ctx->rawParameterSize > (MAX_STACK_ARGS * PARAM_SIZE)) {
            tmpBuffer = alloca(ctx->rawParameterSize);
        }
        (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->rawParameterSize, tmpBuffer);
    }
    ffi_raw_call(&ctx->cif, FFI_FN(ctx->function), retval, (ffi_raw *) tmpBuffer);
}
#else
static inline void
invokeArray(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer, FFIValue* retval)
{
    Function* ctx = (Function *) (uintptr_t) ctxAddress;
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
    void* ffiStackArgs[MAX_STACK_ARGS];
    void** ffiArgs = ffiStackArgs;
    
    if (ctx->cif.nargs > 0) {
        unsigned int i;
        if (ctx->cif.nargs > MAX_STACK_ARGS) {
            tmpBuffer = alloca(ctx->cif.nargs * PARAM_SIZE);
            ffiArgs = alloca(ctx->cif.nargs * sizeof(void *));
        }
        for (i = 0; i < ctx->cif.nargs; ++i) {
            ffiArgs[i] = &tmpBuffer[i * PARAM_SIZE];
        }
        (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->cif.nargs * PARAM_SIZE, tmpBuffer);
    }
    ffi_call(&ctx->cif, FFI_FN(ctx->function), retval, ffiArgs);
}
#endif
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    isRawParameterPackingEnabled
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_com_kenai_jffi_Foreign_isRawParameterPackingEnabled(JNIEnv* env, jobject self)
{
#ifdef USE_RAW
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayInt32
 * Signature: (J[B)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayInt32(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
#if BYTE_ORDER == LITTLE_ENDIAN
    return retval.s32;
#else
    return retval.l & 0xFFFFFFFFL;
#endif
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayInt64
 * Signature: (J[B)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayInt64(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.s64;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayFloat
 * Signature: (J[B)F
 */
JNIEXPORT jfloat JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayFloat(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.f;
}
/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayDouble
 * Signature: (J[B)D
 */
JNIEXPORT jdouble JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayDouble(JNIEnv* env, jclass self, jlong ctxAddress,
        jbyteArray paramBuffer)
{
    FFIValue retval;
    invokeArray(env, ctxAddress, paramBuffer, &retval);
    return retval.d;
}

#define MAX_STACK_OBJECTS (4)
static void
invokeArrayWithObjects(JNIEnv* env, jlong ctxAddress, jbyteArray paramBuffer,
        jint objectCount, jintArray objectInfo, jobjectArray objectArray, FFIValue* retval)
{
    Function* ctx = (Function *) (uintptr_t) ctxAddress;
    union { double d; long long ll; jbyte tmp[PARAM_SIZE]; } tmpStackBuffer[MAX_STACK_ARGS];
    jbyte *tmpBuffer = (jbyte *) &tmpStackBuffer[0];
#if !defined(USE_RAW)
    void* ffiStackArgs[MAX_STACK_ARGS], **ffiArgs = &ffiStackArgs[0];
#endif
    Array stackArrays[MAX_STACK_OBJECTS], *arrays = &stackArrays[0];
    jint stackInfoBuffer[MAX_STACK_OBJECTS * 3], *infoBuffer = &stackInfoBuffer[0];
    StackAllocator alloc;
    unsigned int i, arrayCount = 0;

    if (ctx->cif.nargs > MAX_STACK_ARGS) {
        tmpBuffer = alloca(ctx->cif.nargs * PARAM_SIZE);
    }
#if defined(USE_RAW)
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->rawParameterSize, tmpBuffer);
#else
    if (ctx->cif.nargs > MAX_STACK_ARGS) {
        ffiArgs = alloca(ctx->cif.nargs * sizeof(void *));
    }
    for (i = 0; i < ctx->cif.nargs; ++i) {
        ffiArgs[i] = &tmpBuffer[i * PARAM_SIZE];
    }
    (*env)->GetByteArrayRegion(env, paramBuffer, 0, ctx->cif.nargs * PARAM_SIZE, tmpBuffer);
#endif
    
    if (objectCount > MAX_STACK_OBJECTS) {
        infoBuffer = alloca(objectCount * sizeof(jint) * 3);
        arrays = alloca(objectCount * sizeof(Array));
    }
    initStackAllocator(&alloc);
    (*env)->GetIntArrayRegion(env, objectInfo, 0, objectCount * 3, infoBuffer);
    for (i = 0; i < (unsigned int) objectCount; ++i) {
        int type = infoBuffer[i * 3];
        jsize offset = infoBuffer[(i * 3) + 1];
        jsize length = infoBuffer[(i * 3) + 2];
        jobject object = (*env)->GetObjectArrayElement(env, objectArray, i);
        int idx = (type & com_kenai_jffi_ObjectBuffer_INDEX_MASK) >> com_kenai_jffi_ObjectBuffer_INDEX_SHIFT;
        void* ptr;

        ptr = jffi_getArray(env, object, offset, length, type, &alloc, &arrays[arrayCount]);
        if (ptr == NULL) {
            throwException(env, NullPointer, "Could not allocate array");
            goto cleanup;
        }

        ++arrayCount;
        //printf("array=%p\n", ptr);
#if defined(USE_RAW)
        *((void **)(tmpBuffer + ctx->rawParamOffsets[idx])) = ptr;
#else
        *((void **) ffiArgs[idx]) = ptr;
#endif
    }
#if defined(USE_RAW)
    ffi_raw_call(&ctx->cif, FFI_FN(ctx->function), retval, (ffi_raw *) tmpBuffer);
#else
    ffi_call(&ctx->cif, FFI_FN(ctx->function), retval, ffiArgs);
#endif
cleanup:
    /* Release any array backing memory */
    for (i = 0; i < arrayCount; ++i) {
        if (!arrays[i].stack || arrays[i].mode != JNI_ABORT) {
            //printf("releasing array=%p\n", arrays[i].result);
            (*arrays[i].release)(env, &arrays[i]);
        }
    }
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsInt32
 * Signature: (J[B[I[Ljava/lang/Object;)I
 */
JNIEXPORT jint JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsInt32(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
#if BYTE_ORDER == LITTLE_ENDIAN
    return retval.s32;
#else
    return retval.l & 0xFFFFFFFFL;
#endif
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsInt64
 * Signature: (J[BI[I[Ljava/lang/Object;)J
 */
JNIEXPORT jlong JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsInt64(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.s64;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsFloat
 * Signature: (J[BI[I[Ljava/lang/Object;)F
 */
JNIEXPORT jfloat JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsFloat(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.f;
}

/*
 * Class:     com_kenai_jffi_Foreign
 * Method:    invokeArrayWithObjectsDouble
 * Signature: (J[BI[I[Ljava/lang/Object;)D
 */
JNIEXPORT jdouble JNICALL
Java_com_kenai_jffi_Foreign_invokeArrayWithObjectsDouble(JNIEnv* env, jobject self,
        jlong ctxAddress, jbyteArray paramBuffer, jint objectCount, jintArray objectInfo, jobjectArray objectArray)
{
    FFIValue retval;
    invokeArrayWithObjects(env, ctxAddress, paramBuffer, objectCount, objectInfo, objectArray, &retval);
    return retval.d;
}
