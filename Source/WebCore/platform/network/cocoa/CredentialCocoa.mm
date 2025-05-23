/*
 * Copyright (C) 2009, 2014 Apple Inc. All rights reserved.
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

#import "config.h"
#import "CredentialCocoa.h"

namespace WebCore {

static NSURLCredentialPersistence toNSURLCredentialPersistence(CredentialPersistence persistence)
{
    switch (persistence) {
    case CredentialPersistence::None:
        return NSURLCredentialPersistenceNone;
    case CredentialPersistence::ForSession:
        return NSURLCredentialPersistenceForSession;
    case CredentialPersistence::Permanent:
        return NSURLCredentialPersistencePermanent;
    }

    ASSERT_NOT_REACHED();
    return NSURLCredentialPersistenceNone;
}

static CredentialPersistence toCredentialPersistence(NSURLCredentialPersistence persistence)
{
    switch (persistence) {
    case NSURLCredentialPersistenceNone:
        return CredentialPersistence::None;
    case NSURLCredentialPersistenceForSession:
        return CredentialPersistence::ForSession;
    case NSURLCredentialPersistencePermanent:
    case NSURLCredentialPersistenceSynchronizable:
        return CredentialPersistence::Permanent;
    }

    ASSERT_NOT_REACHED();
    return CredentialPersistence::None;
}

Credential::Credential(const Credential& original, CredentialPersistence persistence)
    : CredentialBase(original, persistence)
{
    RetainPtr originalNSURLCredential = original.m_nsCredential;
    if (!originalNSURLCredential)
        return;

    if (RetainPtr<NSString> user = originalNSURLCredential.get().user)
        m_nsCredential = adoptNS([[NSURLCredential alloc] initWithUser:user.get() password:originalNSURLCredential.get().password persistence:toNSURLCredentialPersistence(persistence)]);
    else if (RetainPtr<SecIdentityRef> identity = originalNSURLCredential.get().identity)
        m_nsCredential = adoptNS([[NSURLCredential alloc] initWithIdentity:identity.get() certificates:originalNSURLCredential.get().certificates persistence:toNSURLCredentialPersistence(persistence)]);
    else {
        // It is not possible to set the persistence of server trust credentials.
        ASSERT_NOT_REACHED();
        m_nsCredential = WTFMove(originalNSURLCredential);
    }
}

Credential::Credential(NSURLCredential *credential)
    : CredentialBase(credential.user, credential.password, toCredentialPersistence(credential.persistence))
    , m_nsCredential(credential)
{
}

NSURLCredential *Credential::nsCredential() const
{
    if (m_nsCredential)
        return m_nsCredential.get();

    if (CredentialBase::isEmpty())
        return nil;

    m_nsCredential = adoptNS([[NSURLCredential alloc] initWithUser:user() password:password() persistence:toNSURLCredentialPersistence(persistence())]);

    return m_nsCredential.get();
}

bool Credential::isEmpty() const
{
    if (m_nsCredential)
        return false;

    return CredentialBase::isEmpty();
}

bool Credential::platformCompare(const Credential& a, const Credential& b)
{
    if (!a.m_nsCredential && !b.m_nsCredential)
        return true;

    return [a.nsCredential() isEqual:b.nsCredential()];
}

bool Credential::encodingRequiresPlatformData(NSURLCredential *credential)
{
    return !credential.user;
}

Credential Credential::fromIPCData(IPCData&& ipcData)
{
    return WTF::switchOn(WTFMove(ipcData), [](NonPlatformData&& data) {
        return Credential { data.user, data.password, data.persistence };
    }, [](RetainPtr<NSURLCredential>&& credential) {
        return Credential { credential.get() };
    });
}

auto Credential::ipcData() const -> IPCData
{
    if (encodingRequiresPlatformData())
        return m_nsCredential;
    return nonPlatformData();
}

} // namespace WebCore
