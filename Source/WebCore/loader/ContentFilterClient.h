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

#if ENABLE(CONTENT_FILTERING)

#include <wtf/AbstractRefCountedAndCanMakeWeakPtr.h>
#include <wtf/Forward.h>

namespace WebCore {
class ContentFilterClient;
}

namespace WTF {
template<typename T> struct IsDeprecatedWeakRefSmartPointerException;
template<> struct IsDeprecatedWeakRefSmartPointerException<WebCore::ContentFilterClient> : std::true_type { };
}

namespace WebCore {

class ContentFilterUnblockHandler;
class ResourceError;
class SharedBuffer;
class SubstituteData;

class ContentFilterClient : public AbstractRefCountedAndCanMakeWeakPtr<ContentFilterClient> {
public:
    virtual ~ContentFilterClient() = default;

    virtual void dataReceivedThroughContentFilter(const SharedBuffer&, size_t) = 0;
    virtual ResourceError contentFilterDidBlock(ContentFilterUnblockHandler, String&& unblockRequestDeniedScript) = 0;
    virtual void cancelMainResourceLoadForContentFilter(const ResourceError&) = 0;
    virtual void handleProvisionalLoadFailureFromContentFilter(const URL& blockedPageURL, SubstituteData&) = 0;

#if HAVE(WEBCONTENTRESTRICTIONS)
    virtual bool usesWebContentRestrictions() = 0;
#endif
};

} // namespace WebCore

#endif // ENABLE(CONTENT_FILTERING)
