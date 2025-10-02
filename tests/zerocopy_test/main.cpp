#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <thread>
#include <timeapi.h>
#include <string>
#include <Pdh.h>
#include <vector>
#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "FDIOComTest.h"

#pragma warning(disable:4996)

int main()
{
	timeBeginPeriod(1);

	BOOL zerocopy = false;
	INT  datasize = 0;
	INT  testtime = 0;
	INT  cnt = 0;
	INT testcnt = 0;
	UINT start;
	FDIOComTest* p = new FDIOComTest;

	wprintf(L" Zero Copy On(1) / Off(0) : ");
	wscanf(L"%d", &zerocopy);
	wprintf(L" Datasize : ");
	wscanf(L"%d", &datasize);
	wprintf(L" Test Time(min) : ");
	wscanf(L"%d", &testtime);
	wprintf(L" Send Thread Count : ");
	wscanf(L"%d", &cnt);
	wprintf(L" Test Count : ");
	wscanf(L"%d", &testcnt);

	for (int i = 0; i < testcnt; i++)
	{
		p->Run(zerocopy, datasize, testtime, cnt);
		start = timeGetTime();
		while (1)
		{
			Sleep(1000);
			if (timeGetTime() - start > testtime * 60 * 1000)
				break;

		}

		p->Stop();
	}
}