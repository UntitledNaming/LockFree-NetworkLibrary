#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <timeapi.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <thread>
#include <Pdh.h>

#pragma comment(lib,"Pdh.lib")

#include "CSector.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CSession.h"
#include "CNetServer.h"
#include "ChatServer.h"
#include "DumpClass.h"

#pragma warning(disable:4996)

CCrashDump dump;

int main()
{
    timeBeginPeriod(1);
    _wsetlocale(LC_ALL, L"korean");
    bool ESC = false;
    ChatServer* pServer = new ChatServer;


    if (!pServer->RunServer())
        __debugbreak();


    while (!ESC)
    {
        Sleep(1000);
        //ESC Å° ´­·¶À¸¸é 
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8001)
        {

            ESC = true;
            continue;
        }

    }
  
    pServer->StopServer();


    wprintf(L"main Thread End...\n");

    return 0;
}


