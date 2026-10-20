/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "types/CType.h"
#include "config/system_constants.h"
#include "view/colors/EColorIndexType.h"

void CType_Markdown::InitTypeConfigImp(STypeConfig* type)
{
	wcscpy(type->m_szTypeName, L"Markdown");
	wcscpy(type->m_szTypeExts, L"md,markdown,mdown");
	type->m_encoding.m_eDefaultCodetype = CODE_UTF8;
	type->m_eDefaultOutline = OUTLINE_TEXT;
	type->m_ColorInfoArr[COLORIDX_SSTRING].m_bDisp = false;
	type->m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp = false;
	const auto bind = [type](int key, int modifiers, EFunctionCode command) {
		type->m_typeKeyBindings[key].m_bOverride[modifiers] = true;
		type->m_typeKeyBindings[key].m_nFuncCodeArr[modifiers] = command;
	};
	bind('B', _CTRL, F_MARKDOWN_BOLD);
	bind('I', _CTRL, F_MARKDOWN_ITALIC);
	bind('K', _CTRL, F_MARKDOWN_LINK);
	bind('K', _CTRL | _SHIFT, F_MARKDOWN_IMAGE);
	bind('X', _CTRL | _SHIFT, F_MARKDOWN_STRIKE);
	bind('Q', _CTRL | _SHIFT, F_MARKDOWN_QUOTE);
	bind('7', _CTRL | _SHIFT, F_MARKDOWN_ORDERED);
	bind('8', _CTRL | _SHIFT, F_MARKDOWN_BULLET);
	bind('9', _CTRL | _SHIFT, F_MARKDOWN_TASK);
	bind(VK_RETURN, _CTRL, F_MARKDOWN_TASKCHECK);
	bind(VK_RETURN, 0, F_MARKDOWN_ENTER);
	bind(VK_TAB, 0, F_MARKDOWN_INDENT);
	bind(VK_TAB, _SHIFT, F_MARKDOWN_UNINDENT);
	bind('E', _CTRL, F_MARKDOWN_CODE);
	bind('C', _CTRL | _SHIFT, F_MARKDOWN_FENCE);
	bind('T', _CTRL | _SHIFT, F_MARKDOWN_TABLE);
	bind('H', _CTRL | _SHIFT, F_MARKDOWN_HR);
	for (int level = 0; level < 6; ++level) {
		bind('1' + level, _CTRL, static_cast<EFunctionCode>(F_MARKDOWN_H1 + level));
	}
}
