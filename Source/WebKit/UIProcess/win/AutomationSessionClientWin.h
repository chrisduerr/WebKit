/*
 * Copyright (C) 2025 Sony Interactive Entertainment Inc.
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

#if ENABLE(REMOTE_INSPECTOR)
#include "APIAutomationSessionClient.h"
#include "WebProcessPool.h"
#include "WebView.h"
#include <JavaScriptCore/RemoteInspectorServer.h>

namespace WebKit {

class AutomationSessionClient final : public API::AutomationSessionClient {
public:
    explicit AutomationSessionClient(const String&, const Inspector::RemoteInspector::Client::SessionCapabilities&);

    String sessionIdentifier() const override { return m_sessionIdentifier; }

    // From API::AutomationSessionClient
    void requestNewPageWithOptions(WebKit::WebAutomationSession&, API::AutomationSessionBrowsingContextOptions, CompletionHandler<void(WebKit::WebPageProxy*)>&&) override;
    void didDisconnectFromRemote(WebKit::WebAutomationSession&) override;

    void retainWebView(Ref<WebView>&&);
    void releaseWebView(WebPageProxy*);

private:
    String m_sessionIdentifier;
    Inspector::RemoteInspector::Client::SessionCapabilities m_capabilities { };

    static void close(WKPageRef, const void*);

    static void didReceiveAuthenticationChallenge(WKPageRef, WKAuthenticationChallengeRef, const void*);
    void didReceiveAuthenticationChallenge(WKPageRef, WKAuthenticationChallengeRef);

    HashSet<Ref<WebView>> m_webViews;
};

} // namespace WebKit

#endif // ENABLE(REMOTE_INSPECTOR)
