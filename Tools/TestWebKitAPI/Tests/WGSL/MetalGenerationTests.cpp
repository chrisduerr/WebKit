/*
 * Copyright (c) 2023 Apple Inc. All rights reserved.
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

#include "config.h"
#include "AST.h"
#include "Lexer.h"
#include "Parser.h"
#include "TestWGSLAPI.h"
#include "WGSLShaderModule.h"

#include <wtf/Assertions.h>

namespace TestWGSLAPI {

inline Expected<String, WGSL::FailedCheck> translate(const String& wgsl, const String& entryPointName)
{
    auto result = WGSL::staticCheck(wgsl, std::nullopt, { 8 });
    if (auto* maybeError = std::get_if<WGSL::FailedCheck>(&result))
        return makeUnexpected(*maybeError);

    auto& ast = std::get<WGSL::SuccessfulCheck>(result).ast;
    auto prepareResult = WGSL::prepare(ast, entryPointName, { });
    if (auto* maybeError = std::get_if<WGSL::Error>(&prepareResult))
        return makeUnexpected(WGSL::FailedCheck { { *maybeError }, { } });
    HashMap<String, WGSL::ConstantValue> constantValues;
    auto generationResult = WGSL::generate(ast, std::get<WGSL::PrepareResult>(prepareResult), constantValues);
    if (auto* maybeError = std::get_if<WGSL::Error>(&generationResult))
        return makeUnexpected(WGSL::FailedCheck { { *maybeError }, { } });
    return { WTFMove(std::get<String>(generationResult)) };
}

TEST(WGSLMetalGenerationTests, RedFrag)
{
    auto mslSource = translate(R"(@fragment
fn main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.0, 0.0, 1.0);
})"_s, "main"_s);

    EXPECT_TRUE(mslSource.has_value());
    EXPECT_EQ(*mslSource, R"(#include <metal_stdlib>
#include <metal_types>

using namespace metal;

template<typename T>
struct __UnpackedTypeImpl;

template<typename T>
struct __PackedTypeImpl;

template<typename T>
using __UnpackedType = typename __UnpackedTypeImpl<T>::Type;

template<typename T>
using __PackedType = typename __PackedTypeImpl<T>::Type;

[[fragment]] vec<float, 4> function0()
{
    return vec<float, 4>(1., 0., 0., 1.);
}

)"_s);
}

TEST(WGSLMetalGenerationTests, BuiltinSampleMask)
{
    auto mslSource = translate(R"(@fragment
fn main(@builtin(position) position : vec4<f32>,
        @builtin(sample_index) sample_index : u32,
        @builtin(sample_mask) sample_mask : u32) {
    let foo : vec4<f32> = position;
    let bar : u32 = (sample_index + sample_mask);
})"_s, "main"_s);

    EXPECT_TRUE(mslSource.has_value());
    EXPECT_EQ(*mslSource, R"(#include <metal_stdlib>
#include <metal_types>

using namespace metal;

template<typename T>
struct __UnpackedTypeImpl;

template<typename T>
struct __PackedTypeImpl;

template<typename T>
using __UnpackedType = typename __UnpackedTypeImpl<T>::Type;

template<typename T>
using __PackedType = typename __PackedTypeImpl<T>::Type;

[[fragment]] void function0(vec<float, 4> parameter0 [[position]], unsigned parameter1 [[sample_id]], unsigned parameter2 [[sample_mask]])
{
    vec<float, 4> local0 = parameter0;
    unsigned local1 = (parameter1 + parameter2);
}

)"_s);
}

}
