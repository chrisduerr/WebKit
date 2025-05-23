/*
 * Copyright (C) 2016-2023 Apple Inc. All rights reserved.
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
#include "PCToCodeOriginMap.h"

#if ENABLE(JIT)

#include "B3PCToOriginMap.h"
#include "DFGNode.h"
#include "LinkBuffer.h"
#include "WasmOpcodeOrigin.h"
#include <wtf/TZoneMallocInlines.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

namespace {

class DeltaCompressionBuilder {
public:
    DeltaCompressionBuilder(size_t maxSize)
        : m_offset(0)
        , m_maxSize(maxSize)
    {
        m_buffer = static_cast<uint8_t*>(fastMalloc(m_maxSize));
    }

    template <typename T>
    void write(T item)
    {
        RELEASE_ASSERT(m_offset + sizeof(T) <= m_maxSize);
        static constexpr uint8_t mask = std::numeric_limits<uint8_t>::max();
        for (unsigned i = 0; i < sizeof(T); i++) {
            *(m_buffer + m_offset) = static_cast<uint8_t>(item & mask);
            item = item >> (sizeof(uint8_t) * 8);
            m_offset += 1;
        }
    }

    uint8_t* m_buffer; 
    size_t m_offset;
    size_t m_maxSize;
};

class DeltaCompresseionReader {
public:
    DeltaCompresseionReader(uint8_t* buffer, size_t size)
        : m_buffer(buffer)
        , m_size(size)
        , m_offset(0)
    { }

    template <typename T>
    T read()
    {
        RELEASE_ASSERT(m_offset + sizeof(T) <= m_size);
        T result = 0;
        for (unsigned i = 0; i < sizeof(T); i++) {
            uint8_t bitsAsInt8 = *(m_buffer + m_offset);
            T bits = static_cast<T>(bitsAsInt8);
            bits = bits << (sizeof(uint8_t) * 8 * i);
            result |= bits;
            m_offset += 1;
        }
        return result;
    }

private:
    uint8_t* m_buffer;
    size_t m_size;
    size_t m_offset;
};

} // anonymous namespace

PCToCodeOriginMapBuilder::PCToCodeOriginMapBuilder(VM& vm)
    : m_shouldBuildMapping(vm.shouldBuilderPCToCodeOriginMapping())
{ }

PCToCodeOriginMapBuilder::PCToCodeOriginMapBuilder(PCToCodeOriginMapBuilder&& other)
    : m_codeRanges(WTFMove(other.m_codeRanges))
    , m_shouldBuildMapping(other.m_shouldBuildMapping)
{ }

#if ENABLE(FTL_JIT)
PCToCodeOriginMapBuilder::PCToCodeOriginMapBuilder(JSTag, VM& vm, const B3::PCToOriginMap& b3PCToOriginMap)
    : m_shouldBuildMapping(vm.shouldBuilderPCToCodeOriginMapping())
{
    if (!m_shouldBuildMapping)
        return;

    for (const B3::PCToOriginMap::OriginRange& originRange : b3PCToOriginMap.ranges()) {
        DFG::Node* node = originRange.origin.dfgOrigin();
        if (node)
            appendItem(originRange.label, node->origin.semantic);
        else
            appendItem(originRange.label, PCToCodeOriginMapBuilder::defaultCodeOrigin());
    }
}
#endif

#if ENABLE(WEBASSEMBLY_OMGJIT)
PCToCodeOriginMapBuilder::PCToCodeOriginMapBuilder(WasmTag, const B3::PCToOriginMap& b3PCToOriginMap)
    : m_shouldBuildMapping(true)
{
    for (const B3::PCToOriginMap::OriginRange& originRange : b3PCToOriginMap.ranges()) {
        B3::Origin b3Origin = originRange.origin;
        if (b3Origin) {
            Wasm::OpcodeOrigin wasmOrigin { b3Origin };
            // We stash the location into a BytecodeIndex.
            appendItem(originRange.label, CodeOrigin(BytecodeIndex(wasmOrigin.location())));
        } else
            appendItem(originRange.label, PCToCodeOriginMapBuilder::defaultCodeOrigin());
    }
}
#endif

void PCToCodeOriginMapBuilder::appendItemSlow(MacroAssembler::Label label, const CodeOrigin& codeOrigin)
{
    if (!m_shouldBuildMapping)
        return;

    if (m_codeRanges.size()) {
        if (m_codeRanges.last().end == label)
            return;
        m_codeRanges.last().end = label;
        if (m_codeRanges.last().codeOrigin == codeOrigin || !codeOrigin)
            return;
    }

    m_codeRanges.append(CodeRange{label, label, codeOrigin});
}


static constexpr uint8_t sentinelPCDelta = 0;
static constexpr int8_t sentinelBytecodeDelta = 0;

PCToCodeOriginMap::PCToCodeOriginMap(PCToCodeOriginMapBuilder&& builder, LinkBuffer& linkBuffer)
{
    RELEASE_ASSERT(builder.didBuildMapping());

    if (!builder.m_codeRanges.size()) {
        m_pcRangeStart = std::numeric_limits<uintptr_t>::max();
        m_pcRangeEnd = std::numeric_limits<uintptr_t>::max();

        m_compressedPCBufferSize = 0;
        m_compressedPCs = nullptr;

        m_compressedCodeOriginsSize = 0;
        m_compressedCodeOrigins = nullptr;

        return;
    }

    // We do a final touch-up on the last range here because of how we generate the table.
    // The final range (if non empty) would be ignored if we didn't append any (arbitrary)
    // range as the last item of the vector.
    PCToCodeOriginMapBuilder::CodeRange& last = builder.m_codeRanges.last();
    if (!(last.start == last.end))
        builder.m_codeRanges.append(PCToCodeOriginMapBuilder::CodeRange{ last.end, last.end, last.codeOrigin }); // This range will never actually be found, but it ensures the real last range is found.

    DeltaCompressionBuilder pcCompressor((sizeof(uintptr_t) + sizeof(uint8_t)) * builder.m_codeRanges.size());
    void* lastPCValue = nullptr;
    auto buildPCTable = [&] (void* pcValue) {
        RELEASE_ASSERT(pcValue > lastPCValue);
        uintptr_t delta = std::bit_cast<uintptr_t>(pcValue) - std::bit_cast<uintptr_t>(lastPCValue);
        RELEASE_ASSERT(delta != sentinelPCDelta);
        lastPCValue = pcValue;
        if (delta > std::numeric_limits<uint8_t>::max()) {
            pcCompressor.write<uint8_t>(sentinelPCDelta);
            pcCompressor.write<uintptr_t>(delta);
            return;
        }

        pcCompressor.write<uint8_t>(static_cast<uint8_t>(delta));
    };

    DeltaCompressionBuilder codeOriginCompressor((sizeof(intptr_t) + sizeof(int8_t) + sizeof(int8_t) + sizeof(InlineCallFrame*)) * builder.m_codeRanges.size());
    CodeOrigin lastCodeOrigin(BytecodeIndex(0));
    auto buildCodeOriginTable = [&] (const CodeOrigin& codeOrigin) {
        intptr_t delta = static_cast<intptr_t>(codeOrigin.bytecodeIndex().offset()) - static_cast<intptr_t>(lastCodeOrigin.bytecodeIndex().offset());
        lastCodeOrigin = codeOrigin;
        if (delta > std::numeric_limits<int8_t>::max() || delta < std::numeric_limits<int8_t>::min() || delta == sentinelBytecodeDelta) {
            codeOriginCompressor.write<int8_t>(sentinelBytecodeDelta);
            codeOriginCompressor.write<intptr_t>(delta);
        } else
            codeOriginCompressor.write<int8_t>(static_cast<int8_t>(delta));

        int8_t hasInlineCallFrameByte = codeOrigin.inlineCallFrame() ? 1 : 0;
        codeOriginCompressor.write<int8_t>(hasInlineCallFrameByte);
        if (hasInlineCallFrameByte)
            codeOriginCompressor.write<uintptr_t>(std::bit_cast<uintptr_t>(codeOrigin.inlineCallFrame()));
    };

    m_pcRangeStart = linkBuffer.locationOf<NoPtrTag>(builder.m_codeRanges.first().start).dataLocation<uintptr_t>();
    m_pcRangeEnd = linkBuffer.locationOf<NoPtrTag>(builder.m_codeRanges.last().end).dataLocation<uintptr_t>();
    m_pcRangeEnd -= 1;

    for (unsigned i = 0; i < builder.m_codeRanges.size(); i++) {
        PCToCodeOriginMapBuilder::CodeRange& codeRange = builder.m_codeRanges[i];
        void* start = linkBuffer.locationOf<NoPtrTag>(codeRange.start).dataLocation();
        void* end = linkBuffer.locationOf<NoPtrTag>(codeRange.end).dataLocation();
        ASSERT(m_pcRangeStart <= std::bit_cast<uintptr_t>(start));
        ASSERT(m_pcRangeEnd >= std::bit_cast<uintptr_t>(end) - 1);
        if (start == end)
            ASSERT(i == builder.m_codeRanges.size() - 1);
        if (i > 0)
            ASSERT(linkBuffer.locationOf<NoPtrTag>(builder.m_codeRanges[i - 1].end).dataLocation() == start);

        buildPCTable(start);
        buildCodeOriginTable(codeRange.codeOrigin);
    }

    m_compressedPCBufferSize = pcCompressor.m_offset;
    m_compressedPCs = static_cast<uint8_t*>(fastRealloc(pcCompressor.m_buffer, m_compressedPCBufferSize));

    m_compressedCodeOriginsSize = codeOriginCompressor.m_offset;
    m_compressedCodeOrigins = static_cast<uint8_t*>(fastRealloc(codeOriginCompressor.m_buffer, m_compressedCodeOriginsSize));
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(PCToCodeOriginMap);

PCToCodeOriginMap::~PCToCodeOriginMap()
{
    if (m_compressedPCs)
        fastFree(m_compressedPCs);
    if (m_compressedCodeOrigins)
        fastFree(m_compressedCodeOrigins);
}

double PCToCodeOriginMap::memorySize()
{
    double size = 0;
    size += m_compressedPCBufferSize;
    size += m_compressedCodeOriginsSize;
    return size;
}

std::optional<CodeOrigin> PCToCodeOriginMap::findPC(void* pc) const
{
    uintptr_t pcAsInt = std::bit_cast<uintptr_t>(pc);
    if (!(m_pcRangeStart <= pcAsInt && pcAsInt <= m_pcRangeEnd))
        return std::nullopt;

    uintptr_t currentPC = 0;
    BytecodeIndex currentBytecodeIndex = BytecodeIndex(0);
    InlineCallFrame* currentInlineCallFrame = nullptr;

    DeltaCompresseionReader pcReader(m_compressedPCs, m_compressedPCBufferSize);
    DeltaCompresseionReader codeOriginReader(m_compressedCodeOrigins, m_compressedCodeOriginsSize);
    while (true) {
        uintptr_t previousPC = currentPC;
        {
            uint8_t value = pcReader.read<uint8_t>();
            uintptr_t delta;
            if (value == sentinelPCDelta)
                delta = pcReader.read<uintptr_t>();
            else
                delta = value;
            currentPC += delta;
        }

        CodeOrigin previousOrigin = CodeOrigin(currentBytecodeIndex, currentInlineCallFrame);
        {
            int8_t value = codeOriginReader.read<int8_t>();
            intptr_t delta;
            if (value == sentinelBytecodeDelta)
                delta = codeOriginReader.read<intptr_t>();
            else
                delta = static_cast<intptr_t>(value);

            currentBytecodeIndex = BytecodeIndex(static_cast<intptr_t>(currentBytecodeIndex.offset()) + delta);

            int8_t hasInlineFrame = codeOriginReader.read<int8_t>();
            ASSERT(hasInlineFrame == 0 || hasInlineFrame == 1);
            if (hasInlineFrame)
                currentInlineCallFrame = std::bit_cast<InlineCallFrame*>(codeOriginReader.read<uintptr_t>());
            else
                currentInlineCallFrame = nullptr;
        }

        if (previousPC) {
            uintptr_t startOfRange = previousPC;
            // We subtract 1 because we generate end points inclusively in this table, even though we are interested in ranges of the form: [previousPC, currentPC)
            uintptr_t endOfRange = currentPC - 1;
            if (startOfRange <= pcAsInt && pcAsInt <= endOfRange)
                return std::optional<CodeOrigin>(previousOrigin); // We return previousOrigin here because CodeOrigin's are mapped to the startValue of the range.
        }
    }

    RELEASE_ASSERT_NOT_REACHED();
    return std::nullopt;
}

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(JIT)
