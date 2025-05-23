/*
 * Copyright (C) 2020 Igalia S.L.
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
#include "LazyLoadFrameObserver.h"

#include "DocumentInlines.h"
#include "HTMLIFrameElement.h"
#include "IntersectionObserverCallback.h"
#include "IntersectionObserverEntry.h"
#include "LocalFrame.h"
#include "RenderStyle.h"

#include <limits>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(LazyLoadFrameObserver);

class LazyFrameLoadIntersectionObserverCallback final : public IntersectionObserverCallback {
public:
    static Ref<LazyFrameLoadIntersectionObserverCallback> create(Document& document)
    {
        return adoptRef(*new LazyFrameLoadIntersectionObserverCallback(document));
    }

private:
    LazyFrameLoadIntersectionObserverCallback(Document& document)
        : IntersectionObserverCallback(&document)
    {
    }

    bool hasCallback() const final { return true; }

    CallbackResult<void> invoke(IntersectionObserver&, const Vector<Ref<IntersectionObserverEntry>>& entries, IntersectionObserver&) final
    {
        ASSERT(!entries.isEmpty());

        for (auto& entry : entries) {
            if (!entry->isIntersecting())
                continue;
            if (RefPtr iframe = dynamicDowncast<HTMLIFrameElement>(entry->target())) {
                iframe->lazyLoadFrameObserver().unobserve();
                iframe->loadDeferredFrame();
            }
        }
        return { };
    }

    CallbackResult<void> invokeRethrowingException(IntersectionObserver& thisObserver, const Vector<Ref<IntersectionObserverEntry>>& entries, IntersectionObserver& observer) final
    {
        return invoke(thisObserver, entries, observer);
    }
};

LazyLoadFrameObserver::LazyLoadFrameObserver(HTMLIFrameElement& element)
    : m_element(element)
{
}

void LazyLoadFrameObserver::observe(const AtomString& frameURL, const ReferrerPolicy& referrerPolicy)
{
    auto& frameObserver = m_element->lazyLoadFrameObserver();
    auto* intersectionObserver = frameObserver.intersectionObserver(m_element->protectedDocument());
    if (!intersectionObserver)
        return;
    m_frameURL = frameURL;
    m_referrerPolicy = referrerPolicy;
    intersectionObserver->observe(m_element);
}

void LazyLoadFrameObserver::unobserve()
{
    auto& frameObserver = m_element->lazyLoadFrameObserver();
    ASSERT(frameObserver.isObserved(m_element));
    frameObserver.m_observer->unobserve(m_element);
}

void LazyLoadFrameObserver::update(const AtomString& frameURL, const ReferrerPolicy& referrerPolicy)
{
    m_frameURL = frameURL;
    m_referrerPolicy = referrerPolicy;
}

IntersectionObserver* LazyLoadFrameObserver::intersectionObserver(Document& document)
{
    if (!m_observer) {
        auto callback = LazyFrameLoadIntersectionObserverCallback::create(document);
        IntersectionObserver::Init options { std::nullopt, emptyString(), { } };
        auto observer = IntersectionObserver::create(document, WTFMove(callback), WTFMove(options));
        if (observer.hasException())
            return nullptr;
        m_observer = observer.releaseReturnValue();
    }
    return m_observer.get();
}

bool LazyLoadFrameObserver::isObserved(Element& element) const
{
    return m_observer && m_observer->isObserving(element);
}

}
