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
	m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WORKERTH_COUNT); // ���� ������ ������ ��Ŀ��Ʈ ������ ����
	if (m_iocp == NULL)
	{
		wprintf(L"CreateIoCompletionPort Failed...\n");
		return;
	}

	SessionInit();
	NetInit();

	// ������ ����
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

	// ����͸� ������, send ������ �ݱ�
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

	// worker thread, accept thread �ݱ�
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

	// ���� ���� ����
	m_listen = socket(AF_INET, SOCK_STREAM, 0);
	if (m_listen == INVALID_SOCKET)
	{
		wprintf(L"socket() error \n");
		__debugbreak();
	}
	wprintf(L"socket() Complete... \n");


	//bind() ó��
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
		wprintf(L"���� ���� ����. ���� �ڵ�: %d \n", err);
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
	//iocp ��ü ����
	m_clientIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WORKERTH_COUNT); // ���� ������ ������ ��Ŀ��Ʈ ������ ����
	if (m_clientIOCP == NULL)
	{
		__debugbreak();
	}

	// ������ ����
	for (int i = 0; i < WORKERTH_COUNT; i++)
	{
		m_clientThread[i] = std::thread(&FDIOComTest::ClientThread, this);
	}

	// Ŭ���̾�Ʈ ��ü �ʱ�ȭ �� IOCP ���
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
		// send flag ���� ������ ���� ����
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

			// client �����尡 �����ϸ鼭 ���� ���� ���̱� ������ send�����忡�� �����Ǹ� �ٷ� �����ؼ� send ������ �����ϱ�
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

		//�񵿱� ���������
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

		// zero copy Ų ���(direct io)
		if (m_zerocopy == true)
		{
			int sendBufferSize = 0;
			setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferSize, sizeof(sendBufferSize));
		}

		// ���̱� ó��
		BOOL bNoDelay = TRUE;
		setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&bNoDelay, sizeof(BOOL));


		linger so_linger;
		so_linger.l_onoff = 1;  // linger �ɼ� ���
		so_linger.l_linger = 0; // ���� �ð� 0 -> ��� RST

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
	WSAOVERLAPPED* pOverlapped = nullptr;     // IO�� ���� Overlapped ����ü �ּҰ� or Task Type

	while (!esc)
	{
		cbTransferred = 0;
		pOverlapped = nullptr;     
		pSession = nullptr;


		retgqcs = GetQueuedCompletionStatus(m_iocp, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

		if (retgqcs == false)
		{
			//IOCP�� �����ų� IOCP �Ϸ� ���� ť���� ��ť�׿� ������ �� ó��
			if (pOverlapped == nullptr)
			{
				esc = true;
				break;
			}

			//WSASend, WSARecv�� ��û�� IO�� ������ ���(TCP�� ���� ����)
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
		WSAOVERLAPPED* pOverlapped = nullptr;//�Ϸ��� wsaoverlapped ����ü �ּҰ�
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
