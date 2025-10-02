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
	// �Է� ����
	//////////////////////////////////////////////////////////////////////
	BOOL m_zerocopy;      // zero copy on/off
	INT  m_sendDataSize;  // ���� ������ ũ��
	INT  m_testTime;      // ������ Send�� �ð�
	INT  m_sendThreadCnt; // send ������ ����

	//////////////////////////////////////////////////////////////////////
	// ���� ����
	//////////////////////////////////////////////////////////////////////
	HANDLE                 m_iocp;                              // IOCP ���ҽ� �ڵ�
	SOCKET                 m_listen;                            // ���� ���� ����
	SESSION                m_sessionTable[MAX_CLIENT_COUNT];    // ���� ���̺�
	std::thread            m_acceptThread;                      // Accept  Thread 
	std::thread*           m_sendThread;                        // WSASend Thread
	std::thread            m_workerThread[WORKERTH_COUNT];                      // GQCS WorkerThread



	//////////////////////////////////////////////////////////////////////
    // Ŭ�� ����(select �� ���)
    //////////////////////////////////////////////////////////////////////
	HANDLE                 m_clientIOCP;
	st_CLIENT              m_client[MAX_CLIENT_COUNT]; // recv�� �� ����
	std::thread            m_clientThread[WORKERTH_COUNT]; // Recv�� �� ����

	//////////////////////////////////////////////////////////////////////
    // ������
    //////////////////////////////////////////////////////////////////////
	UINT64                 m_gqcsTPS;
	UINT64                 m_avgGQCSTPS;
	FLOAT                  m_avgTotalCPU;
	FLOAT                  m_avgUserCPU;
	FLOAT                  m_avgKernelCPU;
	LONG                   m_accept;

	//////////////////////////////////////////////////////////////////////
    // ��Ÿ ����
    //////////////////////////////////////////////////////////////////////
	BOOL                   m_clientflag;
	BOOL                   m_sendflag;
	BOOL                   m_monitorflag;
	BOOL                   m_startflag;              // Accept �����忡�� ���� �� ����Ǹ� Ŵ.
	ProcessMonitor*        m_pPDH;
	std::thread            m_monitorThread;          // Monitor Thread
};

