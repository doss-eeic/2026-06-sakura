/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <array>
#include "util/StaticType.h"

// Shared settings contain names, never process-local font handles or pointers.
using SFontFallbacks = std::array<StaticString<LF_FACESIZE>, 8>;
