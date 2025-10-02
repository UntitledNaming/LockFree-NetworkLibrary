#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <list>
#include <thread>
#include <Pdh.h>
#include <mysql.h>

#pragma comment(lib,"Pdh.lib")

#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "CUser.h"
#include "LogClass.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CSession.h"
#include "CLanServer.h"
#include "CNetServer.h"
#include "CMonitor.h"

#include "DumpClass.h"
#pragma warning(disable:4996)

CCrashDump dump;


int main()
{
    timeBeginPeriod(1);

    bool ESC = false;
    CMonitor* pServer = new CMonitor;

    if (!pServer->RunServer())
        __debugbreak();


    while (!ESC)
    {
        Sleep(1000);

        //ESC 키 눌렀으면 
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8001)
        {
            ESC = true;
            continue;
        }

    }

    pServer->StopServer();


    wprintf(L"main Thread End...\n");
}

