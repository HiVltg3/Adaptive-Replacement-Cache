#pragma once
#include "KICachePolicy.h"
#include "KArcCacheNode.h"
#include "KArcLfuPart.h"
#include "KArcLruPart.h"

namespace KArcCache
{
	template<typename Key, typename Value>
	class KArcCache :public KICachePolicy<Key, Value> {

	};
}