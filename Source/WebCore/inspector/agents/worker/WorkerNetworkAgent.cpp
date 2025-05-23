/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WorkerNetworkAgent.h"

#include "SharedBuffer.h"
#include "WorkerDebuggerProxy.h"
#include "WorkerOrWorkletGlobalScope.h"
#include "WorkerThread.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

using namespace Inspector;

WTF_MAKE_TZONE_ALLOCATED_IMPL(WorkerNetworkAgent);

WorkerNetworkAgent::WorkerNetworkAgent(WorkerAgentContext& context)
    : InspectorNetworkAgent(context, context.globalScope->settingsValues().inspectorMaximumResourcesContentSize)
    , m_globalScope(context.globalScope)
{
    ASSERT(context.globalScope->isContextThread());
}

WorkerNetworkAgent::~WorkerNetworkAgent() = default;

Inspector::Protocol::Network::LoaderId WorkerNetworkAgent::loaderIdentifier(DocumentLoader*)
{
    return { };
}

Inspector::Protocol::Network::FrameId WorkerNetworkAgent::frameIdentifier(DocumentLoader*)
{
    return { };
}

Vector<WebSocket*> WorkerNetworkAgent::activeWebSockets()
{
    // FIXME: <https://webkit.org/b/168475> Web Inspector: Correctly display worker's WebSockets
    return { };
}

void WorkerNetworkAgent::setResourceCachingDisabledInternal(bool disabled)
{
    if (auto* workerDebuggerProxy = m_globalScope->workerOrWorkletThread()->workerDebuggerProxy())
        workerDebuggerProxy->setResourceCachingDisabledByWebInspector(disabled);
}

#if ENABLE(INSPECTOR_NETWORK_THROTTLING)

bool WorkerNetworkAgent::setEmulatedConditionsInternal(std::optional<int>&& /* bytesPerSecondLimit */)
{
    return false;
}

#endif // ENABLE(INSPECTOR_NETWORK_THROTTLING)

ScriptExecutionContext* WorkerNetworkAgent::scriptExecutionContext(Inspector::Protocol::ErrorString&, const Inspector::Protocol::Network::FrameId&)
{
    return m_globalScope.ptr();
}

void WorkerNetworkAgent::addConsoleMessage(std::unique_ptr<Inspector::ConsoleMessage>&& message)
{
    m_globalScope->addConsoleMessage(WTFMove(message));
}

} // namespace WebCore
