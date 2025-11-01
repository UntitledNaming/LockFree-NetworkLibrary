#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <timeapi.h>
#include <iostream>
#include <unordered_map>
#include <list>
#include <thread>
#include <cpp_redis/cpp_redis>

#include "DumpClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "LFQMultiLive.h"
#include "Ring_Buffer.h"
#include "CSession.h"
#include "CNetServer.h"
#include "CLoginServer.h"

CCrashDump dump;


int main()
{
    timeBeginPeriod(1);

    bool ESC = false;
    CLoginServer* pLoginServer;

    pLoginServer = new CLoginServer;
    pLoginServer->RunServer();


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


    pLoginServer->StopServer();

}
