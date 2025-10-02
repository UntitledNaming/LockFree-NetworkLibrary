#pragma once
#include <windows.h>


#pragma pack(push,1)
struct st_LANHEADER
{
	SHORT s_len;
}typedef LANHEADER;
#pragma pack(pop)

#pragma pack(push,1)
struct st_NETHEADER
{
	UCHAR s_code;
	SHORT s_len;
	UCHAR s_randkey;
	UCHAR s_checksum;
}typedef NETHEADER;
#pragma pack(pop)