#pragma once
#define MAX_CLIENT_COUNT 30
#define SERVERPORT       10000
#define WORKERTH_COUNT   3
#define IP_LEN           16

class FDIOComTest
{

private:
	struct st_SESSION
	{
		SOCKET        m_socket;
		LONG          m_sendflag;
		UINT64        m_cnt;
		CHAR* m_dataptr;
		WSAOVERLAPPED m_sendoverlapped;

	}typedef SESSION;

	struct st_CLIENT
	{
		SOCKET m_socket;
		LONG   m_recvflag;
		UINT64 m_cnt;
		CHAR* m_dataptr;
		WSAOVERLAPPED m_recvoverlapped;
	};

public:
	void Run(BOOL zerocopy, INT datasize, INT testTime, INT threadCnt);
	void Stop();

private:
	void SessionInit();
	void NetInit();
	void FileStore();
	void ClientInit();
	void ClientDestroy();
	void SendTest(int threadIndex);
	void RecvPost(st_CLIENT* pclient);

	void AcceptThread();
	void SendThread(int threadIndex);
	void WorkerThread();
	void ClientThread();
	void MonitorThread();

private:
	//////////////////////////////////////////////////////////////////////
	// 입력 변수
	//////////////////////////////////////////////////////////////////////
	BOOL m_zerocopy;      // zero copy on/off
	INT  m_sendDataSize;  // 보낼 데이터 크기
	INT  m_testTime;      // 스레드 Send할 시간
	INT  m_sendThreadCnt; // send 스레드 갯수

	//////////////////////////////////////////////////////////////////////
	// 서버 변수
	//////////////////////////////////////////////////////////////////////
	HANDLE                 m_iocp;                              // IOCP 리소스 핸들
	SOCKET                 m_listen;                            // 서버 리슨 소켓
	SESSION                m_sessionTable[MAX_CLIENT_COUNT];    // 세션 테이블
	std::thread            m_acceptThread;                      // Accept  Thread 
	std::thread*           m_sendThread;                        // WSASend Thread
	std::thread            m_workerThread[WORKERTH_COUNT];                      // GQCS WorkerThread



	//////////////////////////////////////////////////////////////////////
    // 클라 변수(select 모델 사용)
    //////////////////////////////////////////////////////////////////////
	HANDLE                 m_clientIOCP;
	st_CLIENT              m_client[MAX_CLIENT_COUNT]; // recv만 할 예정
	std::thread            m_clientThread[WORKERTH_COUNT]; // Recv만 할 예정

	//////////////////////////////////////////////////////////////////////
    // 측정값
    //////////////////////////////////////////////////////////////////////
	UINT64                 m_gqcsTPS;
	UINT64                 m_avgGQCSTPS;
	FLOAT                  m_avgTotalCPU;
	FLOAT                  m_avgUserCPU;
	FLOAT                  m_avgKernelCPU;
	LONG                   m_accept;

	//////////////////////////////////////////////////////////////////////
    // 기타 변수
    //////////////////////////////////////////////////////////////////////
	BOOL                   m_clientflag;
	BOOL                   m_sendflag;
	BOOL                   m_monitorflag;
	BOOL                   m_startflag;              // Accept 스레드에서 세션 다 연결되면 킴.
	ProcessMonitor*        m_pPDH;
	std::thread            m_monitorThread;          // Monitor Thread
};

