/*
 * Copyright (C) 2024 Devin Rousso <webkit@devinrousso.com>. All rights reserved.
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
#include "JSGenericTypedArrayViewPrototype.h"

#include "ParseInt.h"
#include <wtf/text/Base64.h>

namespace JSC {

JSC_DEFINE_HOST_FUNCTION(uint8ArrayPrototypeToBase64, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSUint8Array* uint8Array = jsDynamicCast<JSUint8Array*>(callFrame->thisValue());
    if (UNLIKELY(!uint8Array))
        return throwVMTypeError(globalObject, scope, "Uint8Array.prototype.toBase64 requires that |this| be a Uint8Array"_s);

    OptionSet<Base64EncodeOption> options;

    JSValue optionsValue = callFrame->argument(0);
    if (!optionsValue.isUndefined()) {
        JSObject* optionsObject = jsDynamicCast<JSObject*>(optionsValue);
        if (UNLIKELY(!optionsObject))
            return throwVMTypeError(globalObject, scope, "Uint8Array.prototype.toBase64 requires that options be an object"_s);

        JSValue alphabetValue = optionsObject->get(globalObject, vm.propertyNames->alphabet);
        RETURN_IF_EXCEPTION(scope, { });
        if (!alphabetValue.isUndefined()) {
            JSString* alphabetString = jsDynamicCast<JSString*>(alphabetValue);
            if (UNLIKELY(!alphabetString))
                return throwVMTypeError(globalObject, scope, "Uint8Array.prototype.toBase64 requires that alphabet be \"base64\" or \"base64url\""_s);

            StringView alphabetStringView = alphabetString->view(globalObject);
            if (alphabetStringView == "base64url"_s)
                options.add(Base64EncodeOption::URL);
            else if (alphabetStringView != "base64"_s)
                return throwVMTypeError(globalObject, scope, "Uint8Array.prototype.toBase64 requires that alphabet be \"base64\" or \"base64url\""_s);
        }

        JSValue omitPaddingValue = optionsObject->get(globalObject, vm.propertyNames->omitPadding);
        RETURN_IF_EXCEPTION(scope, { });
        if (omitPaddingValue.toBoolean(globalObject))
            options.add(Base64EncodeOption::OmitPadding);
    }

    IdempotentArrayBufferByteLengthGetter<std::memory_order_seq_cst> byteLengthGetter;
    if (UNLIKELY(isIntegerIndexedObjectOutOfBounds(uint8Array, byteLengthGetter)))
        return throwVMTypeError(globalObject, scope, typedArrayBufferHasBeenDetachedErrorMessage);

    const uint8_t* data = uint8Array->typedVector();
    size_t length = uint8Array->length();
    RELEASE_AND_RETURN(scope, JSValue::encode(jsString(vm, base64EncodeToString({ data, length }, options))));
}

JSC_DEFINE_HOST_FUNCTION(uint8ArrayPrototypeToHex, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSUint8Array* uint8Array = jsDynamicCast<JSUint8Array*>(callFrame->thisValue());
    if (UNLIKELY(!uint8Array))
        return throwVMTypeError(globalObject, scope, "Uint8Array.prototype.toHex requires that |this| be a Uint8Array"_s);

    IdempotentArrayBufferByteLengthGetter<std::memory_order_seq_cst> byteLengthGetter;
    if (UNLIKELY(isIntegerIndexedObjectOutOfBounds(uint8Array, byteLengthGetter)))
        return throwVMTypeError(globalObject, scope, typedArrayBufferHasBeenDetachedErrorMessage);

    const uint8_t* data = uint8Array->typedVector();
    size_t length = uint8Array->length();
    const auto* end = data + length;

    if (!length)
        return JSValue::encode(jsEmptyString(vm));

    if ((length * 2) > static_cast<size_t>(StringImpl::MaxLength)) {
        throwOutOfMemoryError(globalObject, scope, "generated stirng is too long"_s);
        return { };
    }

    LChar* buffer = nullptr;
    auto result = StringImpl::createUninitialized(length * 2, buffer);
    LChar* bufferEnd = buffer + length * 2;
    constexpr size_t stride = 8; // Because loading uint8x8_t.
    if (length >= stride) {
        auto encodeVector = [&](auto input) {
            // Hex conversion characters are only 16 characters. This perfectly fits in vqtbl1q_u8's table lookup.
            // Thus, this function leverages vqtbl1q_u8 to convert vector characters in a bulk manner.
            //
            // L => low nibble (4bits)
            // H => high nibble (4bits)
            //
            // original uint8x8_t : LHLHLHLHLHLHLHLH
            // widen uint16x8_t   : 00LH00LH00LH00LH00LH00LH00LH00LH
            // high               : LH00LH00LH00LH00LH00LH00LH00LH00
            // low                : 000L000L000L000L000L000L000L000L
            // merged             : LH0LLH0LLH0LLH0LLH0LLH0LLH0LLH0L
            // masked             : 0H0L0H0L0H0L0H0L0H0L0H0L0H0L0H0L
            constexpr simde_uint8x16_t characters { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
            auto widen = simde_vmovl_u8(input);
            auto high = simde_vshlq_n_u16(widen, 8);
            auto low = simde_vshrq_n_u16(widen, 4);
            auto merged = SIMD::bitOr(high, low);
            auto masked = SIMD::bitAnd(simde_vreinterpretq_u8_u16(merged), SIMD::splat<uint8_t>(0xf));
            return simde_vqtbl1q_u8(characters, masked);
        };

        const auto* cursor = data;
        auto* output = buffer;
        for (; cursor + (stride - 1) < end; cursor += stride, output += stride * 2)
            simde_vst1q_u8(output, encodeVector(simde_vld1_u8(cursor)));
        if (cursor < end)
            simde_vst1q_u8(bufferEnd - stride * 2, encodeVector(simde_vld1_u8(end - stride)));
    } else {
        const auto* cursor = data;
        auto* output = buffer;
        for (; cursor < end; cursor += 1, output += 2) {
            auto character = *cursor;
            *output = radixDigits[character / 16];
            *(output + 1) = radixDigits[character % 16];
        }
    }

    return JSValue::encode(jsNontrivialString(vm, WTFMove(result)));
}

} // namespace JSC
