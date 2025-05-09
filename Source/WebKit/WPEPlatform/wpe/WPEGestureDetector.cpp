/*
 * Copyright (C) 2024 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WPEGestureDetector.h"
#include "WPEDisplay.h"
#include "WPESettings.h"

#include <cmath>

namespace WPE {

void GestureDetector::handleEvent(WPEEvent* event)
{
    // Ensure sequence ID is either sole active ID, or zoom sequence ID.
    guint32 sequenceId = wpe_event_touch_get_sequence_id(event);
    if (m_sequenceId && *m_sequenceId != sequenceId
        && (m_gesture == WPE_GESTURE_DRAG
            || (m_zoomSequenceId && *m_zoomSequenceId != sequenceId))) {
        return;
    }

    switch (wpe_event_get_event_type(event)) {
    case WPE_EVENT_TOUCH_DOWN:
        // Get event position.
        double x, y;
        if (!wpe_event_get_position(event, &x, &y)) {
            break;
        }

        // Clear active gesture if no sequence was previously active.
        if (!m_sequenceId && !m_zoomSequenceId) {
            reset();
        }

        // Start a tap gesture, or transition to zoom if a tap is already in progress.
        if (m_gesture == WPE_GESTURE_NONE) {
            m_gesture = WPE_GESTURE_TAP;
        } else {
            m_gesture = WPE_GESTURE_ZOOM;
        }

        // Assign sequence to free touch slot and reset zoom delta.
        if (!m_sequenceId) {
            m_sequenceId = sequenceId;
            m_position = { x, y };
            m_zoomDelta = 1.0;
        } else if (!m_zoomSequenceId) {
            m_zoomSequenceId = sequenceId;
            m_zoomPosition = { x, y };
            m_zoomDelta = 1.0;
        }

        break;
    case WPE_EVENT_TOUCH_CANCEL:
        reset();
        break;
    case WPE_EVENT_TOUCH_MOVE:
        if (double x, y; wpe_event_get_position(event, &x, &y) && m_position && !m_zoomPosition) {
            // Handle drag gesture transitions.
            auto* settings = wpe_display_get_settings(wpe_view_get_display(wpe_event_get_view(event)));
            auto dragActivationThresholdPx = wpe_settings_get_uint32(settings, WPE_SETTING_DRAG_THRESHOLD, nullptr);
            if (m_gesture != WPE_GESTURE_DRAG && std::hypot(x - m_position->x, y - m_position->y) > dragActivationThresholdPx) {
                m_gesture = WPE_GESTURE_DRAG;
                m_nextDeltaReferencePosition = m_position;
                m_dragBegin = true;
            } else if (m_gesture == WPE_GESTURE_DRAG) {
                m_dragBegin = false;
            }

            // Update drag gesture delta.
            if (m_gesture == WPE_GESTURE_DRAG) {
                m_delta = { x - m_nextDeltaReferencePosition->x, y - m_nextDeltaReferencePosition->y };
                m_nextDeltaReferencePosition = { x, y };
            }
        } else if (double x, y; wpe_event_get_position(event, &x, &y) && m_sequenceId && m_zoomSequenceId) {
            // Get previous distance between zoom touch points.
            double lastDistance = std::hypot(
                m_position->x - m_zoomPosition->x,
                m_position->y - m_zoomPosition->y);

            // Update zoom gesture positions.
            if (*m_sequenceId == sequenceId) {
                m_position = { x, y };
            } else if (*m_zoomSequenceId == sequenceId) {
                m_zoomPosition = { x, y };
            }

            // Calculate relative difference to last touch point length.
            double newDistance = std::hypot(
                m_position->x - m_zoomPosition->x,
                m_position->y - m_zoomPosition->y);
            m_zoomDelta = newDistance / lastDistance;
        }

        break;
    case WPE_EVENT_TOUCH_UP:
        if (double x, y; wpe_event_get_position(event, &x, &y) && m_sequenceId) {
            // Handle final drag/zoom position update.
            if (m_gesture == WPE_GESTURE_DRAG) {
                m_delta = { x - m_nextDeltaReferencePosition->x, y - m_nextDeltaReferencePosition->y };
            } else if (m_gesture == WPE_GESTURE_ZOOM && m_zoomSequenceId) {
                // Get previous distance between zoom touch points.
                double lastDistance = std::hypot(
                    m_position->x - m_zoomPosition->x,
                    m_position->y - m_zoomPosition->y);

                if (*m_zoomSequenceId == sequenceId) {
                    m_zoomPosition = { x, y };
                } else {
                    m_position = { x, y };
                }

                // Calculate relative difference to last touch point length.
                double newDistance = std::hypot(
                    m_position->x - m_zoomPosition->x,
                    m_position->y - m_zoomPosition->y);
                m_zoomDelta = newDistance / lastDistance;
            }
        } else {
            reset();
        }

        // Clear active sequence IDs on release.
        if (m_sequenceId && *m_sequenceId == sequenceId) {
            m_sequenceId = std::nullopt;
        } else if (m_zoomSequenceId && *m_zoomSequenceId == sequenceId) {
            m_zoomSequenceId = std::nullopt;
        }

        break;
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

void GestureDetector::reset()
{
    m_gesture = WPE_GESTURE_NONE;
    m_sequenceId = std::nullopt;
    m_position = std::nullopt;
    m_nextDeltaReferencePosition = std::nullopt;
    m_delta = std::nullopt;
    m_dragBegin = std::nullopt;
    m_zoomSequenceId = std::nullopt;
    m_zoomPosition = std::nullopt;
    m_zoomDelta = std::nullopt;
}

} // namespace WPE
