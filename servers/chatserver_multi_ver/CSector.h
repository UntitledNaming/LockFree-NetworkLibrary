#pragma once
#include <windows.h>

#define SECTOR_X_MAX          50
#define SECTOR_Y_MAX          50
#define INVALID_SECTOR_XPOS   10000
#define INVALID_SECTOR_YPOS   10000

struct st_SECTOR
{
	WORD s_xpos;
	WORD s_ypos;
}typedef SECTOR;

struct st_SECTOR_AROUND
{
	INT               s_Cnt;
	SECTOR            s_Around[9];
}typedef SECTOR_AROUND;

