#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <timeapi.h>
#include <thread>
#include <string>
#include <iostream>
#include <unordered_map>
#include <Pdh.h>
#include "ProfilerTLS.h"
#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "FDIOComTest.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib,"Pdh.lib")
#pragma warning(disable:4996)


void FDIOComTest::Run(BOOL zerocopy, INT datasize, INT testTime, INT threadCnt)
{
	if (MAX_CLIENT_COUNT / threadCnt == 0)
		__debugbreak();


	if (zerocopy == 0)
		m_zerocopy = false;
	else
		m_zerocopy = true;

	m_sendThreadCnt = threadCnt;
	m_accept = 0;
	m_gqcsTPS = 0;
	m_avgGQCSTPS = 0;
	m_avgTotalCPU = 0;
	m_avgUserCPU = 0;
	m_avgKernelCPU = 0;
	m_sendDataSize = datasize;
	m_testTime = testTime;
	m_startflag = false;
	m_clientflag = false;
	m_sendflag = false;
	m_monitorflag = false;
	m_pPDH = new ProcessMonitor;
	CProfileTLS::GetInstance()->Init();
	m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WORKERTH_COUNT); // 러닝 스레드 갯수로 컨커런트 스레드 설정
	if (m_iocp == NULL)
	{
		wprintf(L"CreateIoCompletionPort Failed...\n");
		return;
	}

	SessionInit();
	NetInit();

	// 스레드 생성
	m_sendThread = new std::thread[m_sendThreadCnt];
	m_acceptThread = std::thread(&FDIOComTest::AcceptThread, this);
	m_monitorThread = std::thread(&FDIOComTest::MonitorThread, this);
	
	for (int i = 0; i < WORKERTH_COUNT; i++)
	{
		m_workerThread[i] = std::thread(&FDIOComTest::WorkerThread, this);
	}
	for (int i = 0; i < m_sendThreadCnt; i++)
	{
		m_sendThread[i] = std::thread(&FDIOComTest::SendThread, this, i);
	}


	ClientInit();
}

void FDIOComTest::Stop()
{
	std::wstring name;
	name = L"sendTest";
	name += std::to_wstring(m_sendDataSize);
	if (m_zerocopy == true)
		name += L"_zerocopyOn";
	else
		name += L"_zerocopyOff";

	CProfileTLS::GetInstance()->SaveProfilingData(const_cast<WCHAR*>(name.c_str()));
	FileStore();

	ClientDestroy();

	// 모니터링 스레드, send 스레드 닫기
	m_monitorflag = true;
	if (m_monitorThread.joinable())
	{
		m_monitorThread.join();
	}

	m_sendflag = true;
	for (int i = 0; i < m_sendThreadCnt; i++)
	{
		if (m_sendThread[i].joinable())
		{
			m_sendThread[i].join();
		}
	}

	delete[] m_sendThread;
	delete m_pPDH;

	m_sendThread = nullptr;
	m_pPDH = nullptr;

	// worker thread, accept thread 닫기
	CloseHandle(m_iocp);
	closesocket(m_listen);
	m_iocp = NULL;
	m_listen = INVALID_SOCKET;

	if (m_acceptThread.joinable())
	{
		m_acceptThread.join();
	}

	for (int i = 0; i < WORKERTH_COUNT; i++)
	{
		if (m_workerThread[i].joinable())
		{
			m_workerThread[i].join();
		}
	}

	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		free(m_sessionTable[i].m_dataptr);
		m_sessionTable[i].m_dataptr = nullptr;
		closesocket(m_sessionTable[i].m_socket);
	}


	CProfileTLS::GetInstance()->Destroy();

	wprintf(L"stop clear\n");
}

void FDIOComTest::SessionInit()
{
	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		m_sessionTable[i].m_sendflag = 0;
		m_sessionTable[i].m_socket = INVALID_SOCKET;
		m_sessionTable[i].m_dataptr = (char*)malloc(m_sendDataSize);
		m_sessionTable[i].m_cnt = 0;
		memset(m_sessionTable[i].m_dataptr, i, m_sendDataSize);
		ZeroMemory(&m_sessionTable[i].m_sendoverlapped, sizeof(WSAOVERLAPPED));
	}
}

void FDIOComTest::NetInit()
{
	int ret;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		wprintf(L"WSAStartUp Failed....! \n");
		__debugbreak();
	}

	// 리슨 소켓 생성
	m_listen = socket(AF_INET, SOCK_STREAM, 0);
	if (m_listen == INVALID_SOCKET)
	{
		wprintf(L"socket() error \n");
		__debugbreak();
	}
	wprintf(L"socket() Complete... \n");


	//bind() 처리
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SERVERPORT);
	InetPtonW(AF_INET, L"127.0.0.1", &serveraddr.sin_addr);

	ret = bind(m_listen, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (ret == SOCKET_ERROR)
	{
		wprintf(L"bind error : %d\n", WSAGetLastError());
		__debugbreak();
	}

	ret = listen(m_listen, SOMAXCONN);
	if (ret == SOCKET_ERROR)
	{
		__debugbreak();
	}

}

void FDIOComTest::FileStore()
{
	std::wstring filename;
	errno_t err;
	FILE* fp;
	time_t start;
	tm* local_time;
	std::wstring data;

	filename = L"sendTest_Dsize";
	filename += std::to_wstring(m_sendDataSize);
	filename += L"_threadCnt";
	filename += std::to_wstring(m_sendThreadCnt);
	filename += L"_time";
	filename += std::to_wstring(m_testTime);
	if (m_zerocopy == true)
		filename += L"_zerocopyOn.txt";
	else
		filename += L"_zerocopyOff.txt";

	err = _wfopen_s(&fp, filename.c_str(), L"ab");
	if (err != 0)
	{
		wprintf(L"파일 열기 실패. 에러 코드: %d \n", err);
		__debugbreak();
		return;
	}

	start = time(NULL);
	local_time = localtime(&start);
	
	data = L"[" + std::to_wstring(local_time->tm_year + 1900) + L"." + std::to_wstring(local_time->tm_mon + 1) + L"." + std::to_wstring(local_time->tm_mday) + L"." + std::to_wstring(local_time->tm_hour) + L":" + std::to_wstring(local_time->tm_min) + L":" + std::to_wstring(local_time->tm_sec) + L"]";

	data += L"Average Process Total Time : ";
	data += std::to_wstring(m_avgTotalCPU);
	data += L" % / ";

	data += L"Average Process User Time : ";
	data += std::to_wstring(m_avgUserCPU);
	data += L" % / ";

	data += L"Average Process Kernel Time : ";
	data += std::to_wstring(m_avgKernelCPU);
	data += L" % / ";

	data += L"Average GQCS TPS : ";
	data += std::to_wstring(m_avgGQCSTPS);
	data += L" TPS \n";

	fseek(fp, 0, SEEK_END);
	fwrite(data.c_str(), sizeof(WCHAR) * data.size(), 1, fp);

	fclose(fp);
}

void FDIOComTest::ClientInit()
{
	//iocp 객체 생성
	m_clientIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WORKERTH_COUNT); // 러닝 스레드 갯수로 컨커런트 스레드 설정
	if (m_clientIOCP == NULL)
	{
		__debugbreak();
	}

	// 스레드 생성
	for (int i = 0; i < WORKERTH_COUNT; i++)
	{
		m_clientThread[i] = std::thread(&FDIOComTest::ClientThread, this);
	}

	// 클라이언트 객체 초기화 및 IOCP 등록
	HANDLE retCIOCP;
	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		m_client[i].m_socket = socket(AF_INET, SOCK_STREAM, 0);
		if (m_client[i].m_socket == INVALID_SOCKET)
			__debugbreak();

		m_client[i].m_recvflag = 0;
		m_client[i].m_cnt = 0;
		m_client[i].m_dataptr = (CHAR*)malloc(m_sendDataSize);
		ZeroMemory(&m_client[i].m_recvoverlapped, sizeof(WSAOVERLAPPED));

		retCIOCP = CreateIoCompletionPort((HANDLE)m_client[i].m_socket, m_clientIOCP, (ULONG_PTR)&m_client[i], 0);
		if (retCIOCP == NULL)
		{
			__debugbreak();
		}

	}

	// connect
	INT         retval;
	SOCKADDR_IN clientaddr;

	ZeroMemory(&clientaddr, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	clientaddr.sin_port = htons(SERVERPORT);
	InetPtonW(clientaddr.sin_family, L"127.0.0.1", &clientaddr.sin_addr);

	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		retval = connect(m_client[i].m_socket, (SOCKADDR*)&clientaddr, sizeof(clientaddr));
		if (retval == SOCKET_ERROR)
			__debugbreak();

		RecvPost(&m_client[i]);
	}


}

void FDIOComTest::ClientDestroy()
{
	CloseHandle(m_clientIOCP);
	for (int i = 0; i < WORKERTH_COUNT; i++)
	{
		if (m_clientThread[i].joinable())
		{
			m_clientThread[i].join();
		}
	}

	for (int i = 0; i < MAX_CLIENT_COUNT; i++)
	{
		free(m_client[i].m_dataptr);
		m_client[i].m_dataptr = nullptr;
		closesocket(m_client[i].m_socket);
	}
}

void FDIOComTest::SendTest(int threadIndex)
{
	WSABUF wsa;
	INT    ret;
	INT    err;
	INT    idx = threadIndex * MAX_CLIENT_COUNT / m_sendThreadCnt;
	DWORD  flags = 0;

	for (int i = 0; i < MAX_CLIENT_COUNT / m_sendThreadCnt; i++)
	{
		// send flag 켜져 있으면 다음 세션
		if (m_sessionTable[i + idx].m_sendflag != 0)
			continue;

		m_sessionTable[i + idx].m_sendflag = 1;

		wsa.buf = m_sessionTable[i + idx].m_dataptr;
		wsa.len = m_sendDataSize;

		ZeroMemory(&(m_sessionTable[i + idx].m_sendoverlapped),sizeof(WSAOVERLAPPED));

		CProfileTLS::CProfiler profile(L"sendTest");
		ret = WSASend(m_sessionTable[i + idx].m_socket, &wsa, 1, NULL, flags, &(m_sessionTable[i + idx].m_sendoverlapped), NULL);
		if (ret == SOCKET_ERROR)
		{
			err = WSAGetLastError();

			if (err == ERROR_IO_PENDING)
			{
				m_sessionTable[i + idx].m_cnt++;
				continue;
			}

			// client 스레드가 종료하면서 연결 끊을 것이기 때문에 send스레드에서 인지되면 바로 리턴해서 send 스레드 종료하기
			return;
		}

		else if (ret == 0)
		{
			m_sessionTable[i + idx].m_cnt++;
			continue;
		}

		else
			__debugbreak();

	}

}

void FDIOComTest::RecvPost(st_CLIENT* pclient)
{
	INT ret;
	INT err;
	DWORD recv;
	DWORD flags= 0;
	WSABUF wsa;
	wsa.buf = pclient->m_dataptr;
	wsa.len = m_sendDataSize;

	ZeroMemory(&pclient->m_recvoverlapped, sizeof(WSAOVERLAPPED));

	ret = WSARecv(pclient->m_socket, &wsa, 1, &recv, &flags, &pclient->m_recvoverlapped, NULL);
	if (ret == SOCKET_ERROR)
	{
		err = WSAGetLastError();

		//비동기 등록했으면
		if (err == ERROR_IO_PENDING)
		{
			return;
		}

		wprintf(L"ErroCode:%d\n", err);
	}

	else if (ret == 0)
		return;
}

void FDIOComTest::AcceptThread()
{
	wprintf(L"Accept Thread Start... %d \n", GetCurrentThreadId());

	SOCKADDR_IN clientAddr;
	SOCKET client_socket;
	DWORD bytesReceived = 0;
	DWORD flags = 0;
	HANDLE retCIOCP;
	bool esc = false;
	int  addlen = sizeof(clientAddr);
	int  retrel;
	int  sessionidx = 0;

	while (!esc)
	{
		client_socket = accept(m_listen, (SOCKADDR*)&clientAddr, &addlen);
		if (client_socket == INVALID_SOCKET)
		{
			esc = true;
			continue;
		}

		WCHAR szClientIP[IP_LEN] = { 0 };
		InetNtopW(AF_INET, &clientAddr.sin_addr, szClientIP, IP_LEN);

		// zero copy 킨 경우(direct io)
		if (m_zerocopy == true)
		{
			int sendBufferSize = 0;
			setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));
		}

		// 네이글 처리
		BOOL bNoDelay = TRUE;
		setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&bNoDelay, sizeof(BOOL));


		linger so_linger;
		so_linger.l_onoff = 1;  // linger 옵션 사용
		so_linger.l_linger = 0; // 지연 시간 0 -> 즉시 RST

		if (setsockopt(client_socket, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(so_linger)) == SOCKET_ERROR)
		{
			__debugbreak();
		}

		m_sessionTable[sessionidx].m_socket = client_socket;
		ZeroMemory(&m_sessionTable[sessionidx].m_sendoverlapped, sizeof(WSAOVERLAPPED));

		retCIOCP = CreateIoCompletionPort((HANDLE)client_socket, m_iocp, (ULONG_PTR)&m_sessionTable[sessionidx], 0);
		if (retCIOCP == NULL)
		{
			__debugbreak();
		}

		sessionidx++;
		InterlockedIncrement(&m_accept);
		if (sessionidx >= MAX_CLIENT_COUNT)
		{
			m_startflag = true;
		}
		
	}
}

void FDIOComTest::SendThread(int threadIndex)
{
	wprintf(L"send thread start : %d\n", GetCurrentThreadId());

	while (!m_startflag)
	{

	}

	while (!m_sendflag)
	{
		SendTest(threadIndex);
	}

	wprintf(L"send thread end : %d\n", GetCurrentThreadId());
}

void FDIOComTest::WorkerThread()
{
	timeBeginPeriod(1);
	wprintf(L"WorkerThread Start... %d \n", GetCurrentThreadId());


	BOOL           esc = false;
	BOOL           retgqcs = false;
	DWORD          err = -1;
	DWORD          cbTransferred = 0;
	SESSION*       pSession = nullptr;
	WSAOVERLAPPED* pOverlapped = nullptr;     // IO에 사용된 Overlapped 구조체 주소값 or Task Type

	while (!esc)
	{
		cbTransferred = 0;
		pOverlapped = nullptr;     
		pSession = nullptr;


		retgqcs = GetQueuedCompletionStatus(m_iocp, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

		if (retgqcs == false)
		{
			//IOCP가 닫히거나 IOCP 완료 통지 큐에서 디큐잉에 실패할 때 처리
			if (pOverlapped == nullptr)
			{
				esc = true;
				break;
			}

			//WSASend, WSARecv로 요청한 IO가 실패한 경우(TCP가 끊긴 것임)
			else
			{
				err = WSAGetLastError();
				wprintf(L"GQCS Return Error : %d \n", err);
				break;
			}

		}

		if (pSession == nullptr || cbTransferred == 0 || pOverlapped == nullptr)
		{
			__debugbreak();
			continue;
		}

		InterlockedIncrement64((LONG64*) &m_gqcsTPS);

		InterlockedExchange(&(pSession->m_sendflag),0);
	}

	wprintf(L"WorkerThread end... %d \n", GetCurrentThreadId());
}

void FDIOComTest::ClientThread()
{
	INT ret;

	wprintf(L"Client Thread start : %d \n", GetCurrentThreadId());
	
	while (1)
	{
		DWORD          cbTransferred = -1;
		WSAOVERLAPPED* pOverlapped = nullptr;//완료한 wsaoverlapped 구조체 주소값
		st_CLIENT* pClient = nullptr;

		ret = GetQueuedCompletionStatus(m_clientIOCP,&cbTransferred, (PULONG_PTR)&pClient, (LPOVERLAPPED*)&pOverlapped, INFINITE);
		if (ret == false)
		{
			break;
		}

		if (pOverlapped == &pClient->m_recvoverlapped)
		{
			pClient->m_cnt++;
			RecvPost(pClient);
		}

		else
			__debugbreak();
	}

	wprintf(L"Client Thread end : %d \n", GetCurrentThreadId());
}

void FDIOComTest::MonitorThread()
{
	INT loopcnt = 0;
	UINT64 m_gqcstpssum = 0;
	FLOAT m_cputotalsum = 0;
	FLOAT m_cpuusersum = 0;
	FLOAT m_cpukernelsum = 0;

	while (!m_monitorflag)
	{
		loopcnt++;
		Sleep(1000);

		m_pPDH->UpdateCounter();
		m_gqcstpssum += m_gqcsTPS;
		m_cputotalsum += m_pPDH->ProcessTotal();
		m_cpuusersum += m_pPDH->ProcessUser();
		m_cpukernelsum += m_pPDH->ProcessKernel();

		m_avgGQCSTPS = m_gqcstpssum / loopcnt;
		m_avgTotalCPU = m_cputotalsum / loopcnt;
		m_avgUserCPU = m_cpuusersum / loopcnt;
		m_avgKernelCPU = m_cpukernelsum / loopcnt;

		wprintf(L"[ GQCS     TPS   : (Avg %lld , %d) ]\n", m_avgGQCSTPS, m_gqcsTPS);
		wprintf(L"[ Accept Count   : (%d) ]\n",m_accept);
		wprintf(L"[ CPU    Usage   : T[%f%] U[%f%] K[%f%]]\n", m_avgTotalCPU, m_avgUserCPU, m_avgKernelCPU);


		m_gqcsTPS = 0;
	}
}
