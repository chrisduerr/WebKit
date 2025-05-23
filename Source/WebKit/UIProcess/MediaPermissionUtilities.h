/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <WebCore/SecurityOriginData.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Vector.h>

#if PLATFORM(COCOA)
OBJC_CLASS NSString;
#endif

namespace WebCore {
class SecurityOrigin;
}

namespace WebKit {

class WebPageProxy;

enum class MediaPermissionType : uint8_t {
    Audio = 1 << 0,
    Video = 1 << 1
};

enum class MediaPermissionResult {
    Denied,
    Granted,
    Unknown
};

enum class MediaPermissionReason {
    Camera,
    CameraAndMicrophone,
    Microphone,
    DeviceOrientation,
    Geolocation,
    SpeechRecognition,
    ScreenCapture
};

#if PLATFORM(COCOA)
bool checkSandboxRequirementForType(MediaPermissionType);
bool checkUsageDescriptionStringForType(MediaPermissionType);
bool checkUsageDescriptionStringForSpeechRecognition();

RetainPtr<NSString> applicationVisibleNameFromOrigin(const WebCore::SecurityOriginData&);
NSString *applicationVisibleName();
void alertForPermission(WebPageProxy&, MediaPermissionReason, const WebCore::SecurityOriginData&, CompletionHandler<void(bool)>&&);

void requestAVCaptureAccessForType(MediaPermissionType, CompletionHandler<void(bool authorized)>&&);
MediaPermissionResult checkAVCaptureAccessForType(MediaPermissionType);
#endif

#if HAVE(SPEECHRECOGNIZER)
void requestSpeechRecognitionAccess(CompletionHandler<void(bool authorized)>&&);
MediaPermissionResult checkSpeechRecognitionServiceAccess();
bool checkSpeechRecognitionServiceAvailability(const String& localeIdentifier);
#endif

} // namespace WebKit
