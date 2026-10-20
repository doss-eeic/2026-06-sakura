/*! @file */
/*
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "DispPos.h"
#include "doc/layout/CLayout.h"
#include "view/CEditView.h"

void DispPos::SetLayoutLineRef(CLayoutInt line)
{
    m_nLineRef = line;

    m_pcLayoutRef = m_pView
        ? m_pView->SearchDisplayLine(line)
        : CEditDoc::GetInstance(0)->m_cLayoutMgr
            .SearchLineByLayoutY(line);
}

//$$$高速化
void DispPos::ForwardLayoutLineRef(int offset)
{
    SetLayoutLineRef(
        m_nLineRef + CLayoutInt(offset)
    );
}

DispPos::DispPos(
    int nDx,
    int nDy,
    const CEditView* view
)
    : m_pView(view)
    , m_nDx(nDx)
    , m_nDy(nDy)
{
    m_ptDrawOrigin.x = 0;
    m_ptDrawOrigin.y = 0;

    m_ptDrawLayout.x = CLayoutInt(0);
    m_ptDrawLayout.y = CLayoutInt(0);

    SetLayoutLineRef(CLayoutInt(0));
}