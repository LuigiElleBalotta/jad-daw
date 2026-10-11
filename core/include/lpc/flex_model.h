#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lpc/flex_pitch.h"
#include "lpc/flex_time.h"
#include "lpc/model.h"

namespace lpc {

// What a region's flex data (Region::flex) means for the rendering, and how it follows the region when the region is cut, trimmed or joined.
// All positions are frames of the media's sample rate. The region plays the flexed sound that was rendered from `sourceMediaId`; `dst`
// positions are counted from the start of the region (so they equal positions in the rendered file once its offset is taken off).

// The markers as the anchors of a TimeMap (src from the start of the flexed part of the source).
std::vector<FlexAnchor> flexAnchors(const FlexData& f);
// The Flex Pitch notes and what was done to them, in the same order.
std::vector<FlexNote> flexNotes(const FlexData& f);
std::vector<NoteEdit> flexEdits(const FlexData& f);
// Notes from an analysis (no edits yet).
std::vector<FlexNoteData> flexNoteData(const std::vector<FlexNote>& notes);

// The flex data of the region once only its dst frames [from, from + length) remain, the region having had `total` frames before.
// from < 0 and from + length > total add 1:1 sound before and after (taken from the source media). Nullopt when the source media does
// not reach that far (or `length` is not positive).
std::optional<FlexData> flexWindow(const FlexData& f, std::int64_t total, std::int64_t from, std::int64_t length, std::int64_t mediaFrames);

// The flex data of two neighbouring regions put together (`a` has `aTotal` frames); nullopt when the two cannot be one.
std::optional<FlexData> flexJoin(const FlexData& a, std::int64_t aTotal, const FlexData& b);

// Why this flex data cannot be used (empty when it can). `mediaFrames` is the length of the source media.
std::string flexProblem(const FlexData& f, std::int64_t mediaFrames);

}  // namespace lpc
