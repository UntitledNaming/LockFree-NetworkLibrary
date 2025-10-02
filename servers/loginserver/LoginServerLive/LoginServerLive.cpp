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

            ////종료 메세지 보내기(이 함수로 IOCP 완료 통지 큐에 lpOverlapped 가 nullptr이고 key NULL인 완료 통지가 들어감)
            //PostQueuedCompletionStatus(pServer->getIOCPHandle(), NULL, NULL, nullptr);
            ESC = true;
            continue;
        }

    }


    pLoginServer->StopServer();

}
