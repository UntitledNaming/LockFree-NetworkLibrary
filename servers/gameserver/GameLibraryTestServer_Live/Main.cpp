#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <thread>
#include <vector>
#include <unordered_map>
#include <string>

#include "LibraryHeader.h"
#include "LogClass.h"
#include "CGameLibrary.h"
#include "CGroup.h"
#include "DumpClass.h"
#include "CAuth.h"
#include "CEcho.h"
#include "CMonitor.h"

CCrashDump dump;
CGameLibrary* p;

int main()
{
	timeBeginPeriod(1);
	_wsetlocale(LC_ALL, L"korean");
	bool ESC = false;

	// 게임 라이브러리 생성
	p = new CGameLibrary;
	p->Run();

	// 컨텐츠 생성
	CAuth* auth = new CAuth;
	CEcho* echo = new CEcho;
	CMonitor* mo = new CMonitor;


	// Attach
	std::wstring auth_string(L"Auth");
	std::wstring echo_string(L"Echo");
	std::wstring monitor_string(L"Monitor");

	p->Attach(auth, auth_string,25, false);
	p->Attach(echo, echo_string,25, true);
	p->Attach(mo, monitor_string, 1000, true);

	while (!ESC)
	{
		Sleep(1000);

		//ESC 키 눌렀으면 
		if (GetAsyncKeyState(VK_TAB) & 0x8001)
		{

			ESC = true;
			continue;
		}

	}

	p->Stop();

	delete echo;
	delete mo;
	delete auth;

}