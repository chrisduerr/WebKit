/*
 * Copyright (C) 2024 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CSSPropertyParserConsumer+Position.h"

#include "CSSParserContext.h"
#include "CSSParserTokenRange.h"
#include "CSSParserTokenRangeGuard.h"
#include "CSSPrimitiveValue.h"
#include "CSSPropertyParserConsumer+CSSPrimitiveValueResolver.h"
#include "CSSPropertyParserConsumer+Ident.h"
#include "CSSPropertyParserConsumer+LengthPercentageDefinitions.h"
#include "CSSPropertyParserConsumer+MetaConsumer.h"
#include "CSSPropertyParserState.h"
#include "CSSValueKeywords.h"
#include "CSSValuePair.h"
#include "CSSValuePool.h"
#include "RenderStyleConstants.h"

namespace WebCore {
namespace CSSPropertyParserHelpers {

// MARK: <position>
// https://drafts.csswg.org/css-values/#position

// <position> = [
//   [ left | center | right | top | bottom | <length-percentage> ]
// |
//   [ left | center | right ] && [ top | center | bottom ]
// |
//   [ left | center | right | <length-percentage> ]
//   [ top | center | bottom | <length-percentage> ]?
// |
//   [ [ left | right ] <length-percentage> ] &&
//   [ [ top | bottom ] <length-percentage> ]

// MARK: <bg-position>
// https://drafts.csswg.org/css-backgrounds-3/#propdef-background-position

// background-position has special parsing rules, allowing a 3-value syntax:
//
// <bg-position> =  [ left | center | right | top | bottom | <length-percentage> ]
// |
//   [ left | center | right | <length-percentage> ]
//   [ top | center | bottom | <length-percentage> ]
// |
//   [ center | [ left | right ] <length-percentage>? ] &&
//   [ center | [ top | bottom ] <length-percentage>? ]

static RefPtr<CSSPrimitiveValue> consumePositionComponent(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueLeft, CSSValueTop, CSSValueBottom, CSSValueRight, CSSValueCenter>(range);
    return CSSPrimitiveValueResolver<CSS::LengthPercentage<>>::consumeAndResolve(range, state);
}

static bool isHorizontalPositionKeywordOnly(const CSSPrimitiveValue& value)
{
    return value.isValueID() && (value.valueID() == CSSValueLeft || value.valueID() == CSSValueRight);
}

static bool isVerticalPositionKeywordOnly(const CSSPrimitiveValue& value)
{
    return value.isValueID() && (value.valueID() == CSSValueTop || value.valueID() == CSSValueBottom);
}

static PositionCoordinates positionFromOneValue(CSSPrimitiveValue& value)
{
    bool valueAppliesToYAxisOnly = isVerticalPositionKeywordOnly(value);
    if (valueAppliesToYAxisOnly)
        return { CSSPrimitiveValue::create(CSSValueCenter), value };
    return { value, CSSPrimitiveValue::create(CSSValueCenter) };
}

static std::optional<PositionCoordinates> positionFromTwoValues(CSSPrimitiveValue& value1, CSSPrimitiveValue& value2)
{
    bool mustOrderAsXY = isHorizontalPositionKeywordOnly(value1) || isVerticalPositionKeywordOnly(value2) || !value1.isValueID() || !value2.isValueID();
    bool mustOrderAsYX = isVerticalPositionKeywordOnly(value1) || isHorizontalPositionKeywordOnly(value2);
    if (mustOrderAsXY && mustOrderAsYX)
        return std::nullopt;
    if (mustOrderAsYX)
        return PositionCoordinates { value2, value1 };
    return PositionCoordinates { value1, value2 };
}

static std::optional<PositionCoordinates> backgroundPositionFromThreeValues(std::array<RefPtr<CSSPrimitiveValue>, 5>&& values)
{
    RefPtr<CSSValue> resultX;
    RefPtr<CSSValue> resultY;

    RefPtr<CSSPrimitiveValue> center;
    for (int i = 0; values[i]; ++i) {
        auto& currentValue = values[i];
        if (!currentValue->isValueID())
            return std::nullopt;

        auto id = currentValue->valueID();
        if (id == CSSValueCenter) {
            if (center)
                return std::nullopt;
            center = WTFMove(currentValue);
            continue;
        }

        RefPtr<CSSValue> result;
        if (auto& nextValue = values[i + 1]; nextValue && !nextValue->isValueID()) {
            result = CSSValuePair::create(currentValue.releaseNonNull(), nextValue.releaseNonNull());
            ++i;
        } else
            result = WTFMove(currentValue);

        if (id == CSSValueLeft || id == CSSValueRight) {
            if (resultX)
                return std::nullopt;
            resultX = result;
        } else {
            ASSERT(id == CSSValueTop || id == CSSValueBottom);
            if (resultY)
                return std::nullopt;
            resultY = result;
        }
    }

    if (center) {
        ASSERT(resultX || resultY);
        if (resultX && resultY)
            return std::nullopt;
        if (!resultX)
            resultX = WTFMove(center);
        else
            resultY = WTFMove(center);
    }

    return { { resultX.releaseNonNull(), resultY.releaseNonNull() } };
}

static std::optional<PositionCoordinates> positionFromFourValues(std::array<RefPtr<CSSPrimitiveValue>, 5>&& values)
{
    RefPtr<CSSValue> resultX;
    RefPtr<CSSValue> resultY;

    for (int i = 0; values[i]; ++i) {
        auto& currentValue = values[i];
        if (!currentValue->isValueID())
            return std::nullopt;

        auto id = currentValue->valueID();
        if (id == CSSValueCenter)
            return std::nullopt;

        RefPtr<CSSValue> result;
        if (auto& nextValue = values[i + 1]; nextValue && !nextValue->isValueID()) {
            result = CSSValuePair::create(currentValue.releaseNonNull(), nextValue.releaseNonNull());
            ++i;
        } else
            result = WTFMove(currentValue);

        if (id == CSSValueLeft || id == CSSValueRight) {
            if (resultX)
                return std::nullopt;
            resultX = result;
        } else {
            ASSERT(id == CSSValueTop || id == CSSValueBottom);
            if (resultY)
                return std::nullopt;
            resultY = result;
        }
    }

    return PositionCoordinates { resultX.releaseNonNull(), resultY.releaseNonNull() };
}

std::optional<PositionCoordinates> consumeBackgroundPositionCoordinates(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    CSSParserTokenRangeGuard guard { range };

    auto value1 = consumePositionComponent(range, state);
    if (!value1)
        return std::nullopt;

    auto value2 = consumePositionComponent(range, state);
    if (!value2) {
        // NOTE: `positionFromOneValue` always succeeds, so we can commit the range right unconditionally.
        guard.commit();
        return positionFromOneValue(*value1);
    }

    auto value3 = consumePositionComponent(range, state);
    if (!value3) {
        auto result = positionFromTwoValues(*value1, *value2);
        if (result)
            guard.commit();
        return result;
    }

    auto value4 = consumePositionComponent(range, state);

    std::array<RefPtr<CSSPrimitiveValue>, 5> values {
        WTFMove(value1),
        WTFMove(value2),
        WTFMove(value3),
        value4.copyRef(),
        nullptr
    };

    if (value4) {
        auto result = positionFromFourValues(WTFMove(values));
        if (result)
            guard.commit();
        return result;
    }

    auto result = backgroundPositionFromThreeValues(WTFMove(values));
    if (result)
        guard.commit();
    return result;
}

std::optional<PositionCoordinates> consumePositionCoordinates(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    CSSParserTokenRangeGuard guard { range };

    auto value1 = consumePositionComponent(range, state);
    if (!value1)
        return std::nullopt;

    auto value2 = consumePositionComponent(range, state);
    if (!value2) {
        // NOTE: `positionFromOneValue` always succeeds, so we can commit the range right unconditionally.
        guard.commit();
        return positionFromOneValue(*value1);
    }

    auto value3 = consumePositionComponent(range, state);
    if (!value3) {
        auto result = positionFromTwoValues(*value1, *value2);
        if (result)
            guard.commit();
        return result;
    }

    auto value4 = consumePositionComponent(range, state);

    std::array<RefPtr<CSSPrimitiveValue>, 5> values {
        WTFMove(value1),
        WTFMove(value2),
        WTFMove(value3),
        value4.copyRef(),
        nullptr
    };

    if (value4) {
        auto result = positionFromFourValues(WTFMove(values));
        if (result)
            guard.commit();
        return result;
    }

    return std::nullopt;
}

RefPtr<CSSValue> consumePosition(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    if (auto coordinates = consumePositionCoordinates(range, state))
        return CSSValuePair::createNoncoalescing(WTFMove(coordinates->x), WTFMove(coordinates->y));
    return nullptr;
}

std::optional<PositionCoordinates> consumeOneOrTwoValuedPositionCoordinates(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    auto value1 = consumePositionComponent(range, state);
    if (!value1)
        return std::nullopt;
    auto value2 = consumePositionComponent(range, state);
    if (!value2)
        return positionFromOneValue(*value1);
    return positionFromTwoValues(*value1, *value2);
}

static RefPtr<CSSValue> consumeSingleAxisPosition(CSSParserTokenRange& range, CSS::PropertyParserState& state, BoxOrient orientation)
{
    RefPtr<CSSPrimitiveValue> value1;

    if (range.peek().type() == IdentToken) {
        switch (orientation) {
        case BoxOrient::Horizontal:
            value1 = consumeIdent<CSSValueLeft, CSSValueRight, CSSValueCenter>(range);
            break;
        case BoxOrient::Vertical:
            value1 = consumeIdent<CSSValueTop, CSSValueBottom, CSSValueCenter>(range);
            break;
        }
        if (!value1)
            return nullptr;

        if (value1->valueID() == CSSValueCenter)
            return value1;
    }

    auto value2 = CSSPrimitiveValueResolver<CSS::LengthPercentage<>>::consumeAndResolve(range, state);
    if (value1 && value2)
        return CSSValuePair::create(value1.releaseNonNull(), value2.releaseNonNull());

    return value1 ? value1 : value2;
}

RefPtr<CSSValue> consumePositionX(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    return consumeSingleAxisPosition(range, state, BoxOrient::Horizontal);
}

RefPtr<CSSValue> consumePositionY(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    return consumeSingleAxisPosition(range, state, BoxOrient::Vertical);
}

// MARK: Unresolved Position

using PositionUnresolvedComponent = std::variant<CSS::Keyword::Left, CSS::Keyword::Right, CSS::Keyword::Top, CSS::Keyword::Bottom, CSS::Keyword::Center, CSS::LengthPercentage<>>;

static std::optional<PositionUnresolvedComponent> consumePositionUnresolvedComponent(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    if (range.peek().type() == IdentToken) {
        switch (range.peek().id()) {
        case CSSValueLeft:
            range.consumeIncludingWhitespace();
            return PositionUnresolvedComponent { CSS::Keyword::Left { } };
        case CSSValueRight:
            range.consumeIncludingWhitespace();
            return PositionUnresolvedComponent { CSS::Keyword::Right { } };
        case CSSValueBottom:
            range.consumeIncludingWhitespace();
            return PositionUnresolvedComponent { CSS::Keyword::Bottom { } };
        case CSSValueTop:
            range.consumeIncludingWhitespace();
            return PositionUnresolvedComponent { CSS::Keyword::Top { } };
        case CSSValueCenter:
            range.consumeIncludingWhitespace();
            return PositionUnresolvedComponent { CSS::Keyword::Center { } };
        default:
            return std::nullopt;
        }
    }

    if (auto lengthPercentage = MetaConsumer<CSS::LengthPercentage<>>::consume(range, state))
        return PositionUnresolvedComponent { WTFMove(*lengthPercentage) };
    return std::nullopt;
}

std::optional<CSS::TwoComponentPositionHorizontal> consumeTwoComponentPositionHorizontalUnresolved(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    if (range.peek().type() == IdentToken) {
        switch (range.peek().id()) {
        case CSSValueLeft:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionHorizontal { CSS::Keyword::Left { } };
        case CSSValueRight:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionHorizontal { CSS::Keyword::Right { } };
        case CSSValueCenter:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionHorizontal { CSS::Keyword::Center { } };
        default:
            return std::nullopt;
        }
    }

    if (auto lengthPercentage = MetaConsumer<CSS::LengthPercentage<>>::consume(range, state))
        return CSS::TwoComponentPositionHorizontal { WTFMove(*lengthPercentage) };
    return std::nullopt;
}

std::optional<CSS::TwoComponentPositionVertical> consumeTwoComponentPositionVerticalUnresolved(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    if (range.peek().type() == IdentToken) {
        switch (range.peek().id()) {
        case CSSValueBottom:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionVertical { CSS::Keyword::Bottom { } };
        case CSSValueTop:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionVertical { CSS::Keyword::Top { } };
        case CSSValueCenter:
            range.consumeIncludingWhitespace();
            return CSS::TwoComponentPositionVertical { CSS::Keyword::Center { } };
        default:
            return std::nullopt;
        }
    }

    if (auto lengthPercentage = MetaConsumer<CSS::LengthPercentage<>>::consume(range, state))
        return CSS::TwoComponentPositionVertical { WTFMove(*lengthPercentage) };
    return std::nullopt;
}

static bool isHorizontalPositionKeywordOnly(const PositionUnresolvedComponent& component)
{
    return std::holds_alternative<CSS::Keyword::Left>(component) || std::holds_alternative<CSS::Keyword::Right>(component);
}

static bool isVerticalPositionKeywordOnly(const PositionUnresolvedComponent& component)
{
    return std::holds_alternative<CSS::Keyword::Top>(component) || std::holds_alternative<CSS::Keyword::Bottom>(component);
}

static CSS::Position positionUnresolvedFromOneComponent(PositionUnresolvedComponent&& component)
{
    return WTF::switchOn(WTFMove(component),
        [](CSS::Keyword::Left&& component) {
            return CSS::TwoComponentPosition { { WTFMove(component) }, { CSS::Keyword::Center { } } };
        },
        [](CSS::Keyword::Right&& component) {
            return CSS::TwoComponentPosition { { WTFMove(component) }, { CSS::Keyword::Center { } } };
        },
        [](CSS::Keyword::Top&& component) {
            return CSS::TwoComponentPosition { { CSS::Keyword::Center { } }, { WTFMove(component) } };
        },
        [](CSS::Keyword::Bottom&& component) {
            return CSS::TwoComponentPosition { { CSS::Keyword::Center { } }, { WTFMove(component) } };
        },
        [](CSS::Keyword::Center&&) {
            return CSS::TwoComponentPosition { { CSS::Keyword::Center { } }, { CSS::Keyword::Center { } } };
        },
        [](CSS::LengthPercentage<>&& component) {
            return CSS::TwoComponentPosition { { WTFMove(component) }, { CSS::Keyword::Center { } } };
        }
    );
}

static CSS::TwoComponentPositionHorizontal toTwoComponentPositionHorizontal(PositionUnresolvedComponent&& component)
{
    return WTF::switchOn(WTFMove(component),
        [](CSS::Keyword::Top&&) {
            ASSERT_NOT_REACHED();
            return CSS::TwoComponentPositionHorizontal { CSS::Keyword::Center { } };
        },
        [](CSS::Keyword::Bottom&&) {
            ASSERT_NOT_REACHED();
            return CSS::TwoComponentPositionHorizontal { CSS::Keyword::Center { } };
        },
        [](auto&& component) {
            return CSS::TwoComponentPositionHorizontal { WTFMove(component) };
        }
    );
}

static CSS::TwoComponentPositionVertical toTwoComponentPositionVertical(PositionUnresolvedComponent&& component)
{
    return WTF::switchOn(WTFMove(component),
        [](CSS::Keyword::Left&&) {
            ASSERT_NOT_REACHED();
            return CSS::TwoComponentPositionVertical { CSS::Keyword::Center { } };
        },
        [](CSS::Keyword::Right&&) {
            ASSERT_NOT_REACHED();
            return CSS::TwoComponentPositionVertical { CSS::Keyword::Center { } };
        },
        [](auto&& component) {
            return CSS::TwoComponentPositionVertical { WTFMove(component) };
        }
    );
}

static std::optional<CSS::Position> positionUnresolvedFromTwoComponents(PositionUnresolvedComponent&& component1, PositionUnresolvedComponent&& component2)
{
    bool mustOrderAsXY = isHorizontalPositionKeywordOnly(component1) || isVerticalPositionKeywordOnly(component2) || std::holds_alternative<CSS::LengthPercentage<>>(component1) || std::holds_alternative<CSS::LengthPercentage<>>(component2);
    bool mustOrderAsYX = isVerticalPositionKeywordOnly(component1) || isHorizontalPositionKeywordOnly(component2);
    if (mustOrderAsXY && mustOrderAsYX)
        return std::nullopt;
    if (mustOrderAsYX)
        return CSS::TwoComponentPosition { toTwoComponentPositionHorizontal(WTFMove(component2)), toTwoComponentPositionVertical(WTFMove(component1)) };
    return CSS::TwoComponentPosition { toTwoComponentPositionHorizontal(WTFMove(component1)), toTwoComponentPositionVertical(WTFMove(component2)) };
}

static std::optional<CSS::Position> positionUnresolvedFromFourComponents(PositionUnresolvedComponent&& component1, PositionUnresolvedComponent&& component2, PositionUnresolvedComponent&& component3, PositionUnresolvedComponent&& component4)
{
    std::optional<CSS::FourComponentPositionHorizontal> horizontal;
    std::optional<CSS::FourComponentPositionVertical> vertical;

    WTF::switchOn(WTFMove(component1),
        [&](CSS::Keyword::Left&& component) {
            if (!std::holds_alternative<CSS::LengthPercentage<>>(component2))
                return;
            horizontal = CSS::FourComponentPositionHorizontal { component, std::get<CSS::LengthPercentage<>>(component2) };
        },
        [&](CSS::Keyword::Right&& component) {
            if (!std::holds_alternative<CSS::LengthPercentage<>>(component2))
                return;
            horizontal = CSS::FourComponentPositionHorizontal { component, std::get<CSS::LengthPercentage<>>(component2) };
        },
        [&](CSS::Keyword::Top&& component) {
            if (!std::holds_alternative<CSS::LengthPercentage<>>(component2))
                return;
            vertical = CSS::FourComponentPositionVertical { component, std::get<CSS::LengthPercentage<>>(component2) };
        },
        [&](CSS::Keyword::Bottom&& component) {
            if (!std::holds_alternative<CSS::LengthPercentage<>>(component2))
                return;
            vertical = CSS::FourComponentPositionVertical { component, std::get<CSS::LengthPercentage<>>(component2) };
        },
        [&](CSS::Keyword::Center&&) { },
        [&](CSS::LengthPercentage<>&&) { }
    );

    if (!horizontal && !vertical)
        return std::nullopt;

    WTF::switchOn(WTFMove(component3),
        [&](CSS::Keyword::Left&& component) {
            if (horizontal || !std::holds_alternative<CSS::LengthPercentage<>>(component4))
                return;
            horizontal = CSS::FourComponentPositionHorizontal { component, std::get<CSS::LengthPercentage<>>(component4) };
        },
        [&](CSS::Keyword::Right&& component) {
            if (horizontal || !std::holds_alternative<CSS::LengthPercentage<>>(component4))
                return;
            horizontal = CSS::FourComponentPositionHorizontal { component, std::get<CSS::LengthPercentage<>>(component4) };
        },
        [&](CSS::Keyword::Top&& component) {
            if (vertical || !std::holds_alternative<CSS::LengthPercentage<>>(component4))
                return;
            vertical = CSS::FourComponentPositionVertical { component, std::get<CSS::LengthPercentage<>>(component4) };
        },
        [&](CSS::Keyword::Bottom&& component) {
            if (vertical || !std::holds_alternative<CSS::LengthPercentage<>>(component4))
                return;
            vertical = CSS::FourComponentPositionVertical { component, std::get<CSS::LengthPercentage<>>(component4) };
        },
        [&](CSS::Keyword::Center&&) { },
        [&](CSS::LengthPercentage<>&&) { }
    );

    if (!horizontal || !vertical)
        return std::nullopt;

    return CSS::FourComponentPosition { WTFMove(*horizontal), WTFMove(*vertical) };
}

std::optional<CSS::Position> consumePositionUnresolved(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    auto rangeCopy = range;

    auto component1 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component1)
        return std::nullopt;

    auto component2 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component2) {
        range = rangeCopy;
        return positionUnresolvedFromOneComponent(WTFMove(*component1));
    }

    auto component3 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component3) {
        auto position = positionUnresolvedFromTwoComponents(WTFMove(*component1), WTFMove(*component2));
        if (!position)
            return std::nullopt;
        range = rangeCopy;
        return position;
    }

    auto component4 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component4)
        return std::nullopt;

    auto position = positionUnresolvedFromFourComponents(WTFMove(*component1), WTFMove(*component2), WTFMove(*component3), WTFMove(*component4));
    if (!position)
        return std::nullopt;
    range = rangeCopy;
    return position;
}

std::optional<CSS::Position> consumeOneOrTwoComponentPositionUnresolved(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    auto rangeCopy = range;

    auto component1 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component1)
        return std::nullopt;

    auto component2 = consumePositionUnresolvedComponent(rangeCopy, state);
    if (!component2) {
        range = rangeCopy;
        return positionUnresolvedFromOneComponent(WTFMove(*component1));
    }

    auto position = positionUnresolvedFromTwoComponents(WTFMove(*component1), WTFMove(*component2));
    if (!position)
        return std::nullopt;
    range = rangeCopy;
    return position;
}

} // namespace CSSPropertyParserHelpers
} // namespace WebCore
