/*
 * Copyright (C) 2011 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "JSWrappable.h"
#include <JavaScriptCore/JSObjectRef.h>
#include <JavaScriptCore/JSRetainPtr.h>
#include <wtf/Condition.h>
#include <wtf/FastMalloc.h>
#include <wtf/Platform.h>
#if PLATFORM(COCOA)
#include <wtf/RetainPtr.h>
#endif
#include <wtf/RunLoop.h>
#include <wtf/Threading.h>

namespace WTR {

class AccessibilityUIElement;
#if USE(ATSPI)
class AccessibilityNotificationHandler;
#endif

class AccessibilityController : public JSWrappable {
public:
    static Ref<AccessibilityController> create();
    ~AccessibilityController();

    void setRetainedElement(AccessibilityUIElement*);
    AccessibilityUIElement* retainedElement() { return m_retainedElement.get(); }

    void makeWindowObject(JSContextRef);
    virtual JSClassRef wrapperClass();

    // Enhanced accessibility.
    void enableEnhancedAccessibility(bool);
    bool enhancedAccessibilityEnabled();

    void setIsolatedTreeMode(bool);
    void setForceDeferredSpellChecking(bool);
    void setForceInitialFrameCaching(bool);

    JSRetainPtr<JSStringRef> platformName();

    // Controller Methods - platform-independent implementations.
    Ref<AccessibilityUIElement> rootElement(JSContextRef);
    RefPtr<AccessibilityUIElement> focusedElement(JSContextRef);
    RefPtr<AccessibilityUIElement> elementAtPoint(JSContextRef, int x, int y);
    RefPtr<AccessibilityUIElement> accessibleElementById(JSContextRef, JSStringRef idAttribute);
    void announce(JSStringRef);

#if PLATFORM(COCOA)
    void executeOnAXThreadAndWait(Function<void()>&&);
    void executeOnAXThread(Function<void()>&&);
    void executeOnMainThread(Function<void()>&&);
#endif

    bool addNotificationListener(JSContextRef, JSValueRef functionCallback);
    bool removeNotificationListener();
    void injectAccessibilityPreference(JSStringRef domain, JSStringRef key, JSStringRef value);

    // Here for consistency with DRT. Not implemented because they don't do anything on the Mac.
    void logFocusEvents() { }
    void logValueChangeEvents() { }
    void logScrollingStartEvents() { }
    void logAccessibilityEvents() { };
#if PLATFORM(MAC)
    void printTrees(JSContextRef);
#else
    void printTrees(JSContextRef) { }
#endif

    void resetToConsistentState();

    void overrideClient(JSStringRef clientType);

private:
    AccessibilityController();
    void platformInitialize();

#if PLATFORM(COCOA)
    RetainPtr<id> m_globalNotificationHandler;
#elif USE(ATSPI)
    std::unique_ptr<AccessibilityNotificationHandler> m_globalNotificationHandler;
#endif

    RefPtr<AccessibilityUIElement> m_retainedElement;

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    void updateIsolatedTreeMode();

#if PLATFORM(COCOA)
    void spinMainRunLoop() const;
#endif

    bool m_accessibilityIsolatedTreeMode { false };
#endif
};

#if PLATFORM(COCOA)

class AXThread {
    WTF_MAKE_NONCOPYABLE(AXThread);

public:
    static bool isCurrentThread();
    static void dispatch(Function<void()>&&);

    // Will dispatch the given function on the main thread once all pending functions
    // on the AX thread have finished executing. Used for synchronization purposes.
    static void dispatchBarrier(Function<void()>&&);

private:
    friend NeverDestroyed<AXThread>;

    AXThread();

    static AXThread& singleton();

    void createThreadIfNeeded();
    void dispatchFunctionsFromAXThread();

    void initializeRunLoop();
    void wakeUpRunLoop();

#if PLATFORM(COCOA)
    static void threadRunLoopSourceCallback(void* AXThread);
    void threadRunLoopSourceCallback();
#endif

    RefPtr<Thread> m_thread;

    Condition m_initializeRunLoopConditionVariable;
    Lock m_initializeRunLoopMutex;

    Lock m_functionsMutex;
    Vector<Function<void()>> m_functions;

#if PLATFORM(COCOA)
    // FIXME: We should use WebCore::RunLoop here.
    RetainPtr<CFRunLoopRef> m_threadRunLoop;
    RetainPtr<CFRunLoopSourceRef> m_threadRunLoopSource;
#else
    RunLoop* m_runLoop { nullptr };
#endif
};

#endif // PLATFORM(COCOA)

} // namespace WTR
