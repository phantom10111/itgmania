#include "global.h"
#include "LuaExpressionTransform.h"
#include "LuaManager.h"
#include "RageUtil.h"

LuaExpressionTransform::LuaExpressionTransform()
{
}

LuaExpressionTransform::~LuaExpressionTransform()
{
}

void LuaExpressionTransform::SetFromReference( const LuaReference &ref )
{
	m_exprTransformFunction = ref;
}

void LuaExpressionTransform::TransformItem( Actor &a, float fPositionOffsetFromCenter, int iItemIndex, int iNumItems ) const
{
	a.DestTweenState().Init();
	Lua *L = LUA->Get();
	m_exprTransformFunction.PushSelf( L );
	ASSERT( !lua_isnil(L, -1) );
	a.PushSelf( L );
	LuaHelpers::Push( L, fPositionOffsetFromCenter );
	LuaHelpers::Push( L, iItemIndex );
	LuaHelpers::Push( L, iNumItems );
	RString error= "Lua error in Transform function: ";
	LuaHelpers::RunScriptOnStack(L, error, 4, 0, true);
	LUA->Release(L);
}

const Actor::TweenState &LuaExpressionTransform::GetTransform( float fPositionOffsetFromCenter, int iItemIndex, int iNumItems ) const
{
	// This can be called many times per frame so allocating an Actor each time can be a huge performance hit.
	// Make the actor static to offset this a little bit.
	static Actor a;
	TransformItem( a, fPositionOffsetFromCenter, iItemIndex, iNumItems );
	return a.DestTweenState();
}


/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
