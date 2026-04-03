#pragma once

#include "n0s/vendor/rapidjson/document.h"
#include "n0s/vendor/rapidjson/error/en.h"

using namespace rapidjson;

/* This macro brings rapidjson more in line with other libs */
inline const Value* GetObjectMember(const Value& obj, const char* key)
{
	Value::ConstMemberIterator itr = obj.FindMember(key);
	if(itr != obj.MemberEnd())
		return &itr->value;
	else
		return nullptr;
}

#ifdef _WIN32
#include <cstdlib>
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#include <byteswap.h>
#endif
