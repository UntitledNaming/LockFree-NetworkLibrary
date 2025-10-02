#pragma once

#define MAX_THREAD_COUNT   100
#define IP_LEN             16
#define INDEX_POS          48
#define INVALID_ID         -1
#define SENDQ_MAX_SIZE     10000

class CSession;

class CNetServer
{
private:
	struct st_IOFLAG
	{
		long long Cnt;
		long long Flag;
	};


public:
	enum PQCS_TYPE
	{
		en_RELEASE = 1,
	};


private:
	HANDLE                 m_IOCP;                  // IOCP 리소스 핸들
	SOCKET                 m_Listen;                // 서버 리슨 소켓

	///////////////////////////////////////////////////////////////////////////////////////
	// Config 정보
	///////////////////////////////////////////////////////////////////////////////////////
	std::string            m_IP;                    // 서버 IP
	INT                    m_Port;                  // 서버 Port
	INT                    m_MaxSessionCnt;         // 최대 세션 갯수 설정
	INT                    m_CreateWorkerCnt;       // 생성 워커 스레드 갯수
	INT                    m_ConcurrentCnt;         // IOCP 러닝 스레드 갯수
	INT                    m_SendFrame;             // Send 스레드 Sleep값
	INT                    m_PacketCode;            // 네트워크 라이브러리 패킷 코드
	INT                    m_FixedKey;              // 인코딩 고정 키 값
	INT                    m_SendThFL;              // Send 스레드 생성 버전 플래그 ( 0 : 끄기 / 1 : 켜기 )
	INT                    m_Nagle;                 // 네이글 옵션

public:
	///////////////////////////////////////////////////////////////////////////////////////
	// 모니터링 변수
	///////////////////////////////////////////////////////////////////////////////////////
	LONG                   m_AcceptTPS;             // 초당 Accept 처리 횟수 모니터링 용
	LONG                   m_RecvIOTPS;             // 초당 Recv 처리 횟수 모니터링 용
	LONG                   m_SendIOTPS;             // 초당 Send 처리 횟수 모니터링 용
	INT64                  m_AcceptTotal;           // Accept처리한 최대 세션 갯수
	SHORT                  m_CurSessionCnt;         // 세션 배열에서 실제로 사용중인 세션 갯수


private:
	///////////////////////////////////////////////////////////////////////////////////////
	// 스레드 정보 및 자료구조
	///////////////////////////////////////////////////////////////////////////////////////
	std::thread            m_IOCPWorkerThread[MAX_THREAD_COUNT]; // Worker 관리 배열
	std::thread            m_AcceptThread;                       // AcceptThread 관리
	std::thread            m_SendThread;                         // SendThread


	///////////////////////////////////////////////////////////////////////////////////////
	// 세션 관리 자료구조 및 멤버 변수
	///////////////////////////////////////////////////////////////////////////////////////
	CSession*              m_SessionTable;      	             // 세션 구조체를 관리할 배열
	LFStack<UINT16>*       m_pSessionIdxStack;                   // 세션 배열의 index 관리할 자료구조
	UINT64                 m_AllocID;                            // 세션에게 할당한 세션 ID

public:
	         CNetServer();
	virtual ~CNetServer();

public:
	// 외부 제공 함수 //

	bool          Start(WCHAR* SERVERIP, int SERVERPORT, int numberOfCreateThread, int numberOfRunningThread, int maxNumOfSession, int SendSleep, int SendTHFL , WORD packetCode, WORD fixedkey, bool OffNagle);
	bool          Disconnect(UINT64 SessionID);
	bool          SendPacket(UINT64 SessionID, CMessage* pMessage);
	bool          SendPacketAll(CMessage* pMessage);
	bool          FindIP(UINT64 SessionID, WCHAR* OutIP);

	void          Stop();

protected:
	virtual bool  OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort) = 0;

	virtual void  OnClientJoin(UINT64 SessionID) = 0;

	virtual void  OnClientLeave(UINT64 SessionID) = 0;

	virtual void  OnRecv(UINT64 SessionID, CMessage* pMessage) = 0;

private:
	void          WorkerThread();
	void          AcceptThread();
	void          SendThread();
	
public:
	//------------------------------------------------------------------------------
	// 네트워크 라이브에서만 사용할 함수
	//------------------------------------------------------------------------------
	void          Net_Init(WCHAR* SERVERIP, int SERVERPORT, bool OffNagle);
	void          Mem_Init();
	void          Thread_Create();
	void          Thread_Destroy();
	void          FindSession(UINT64 SessionID, CSession** ppSession); // 아웃파라미터 nullptr이면 세션ID가 Invalid임.
	UINT64        MakeSessionID(UINT16 index, UINT64 allocID);

	bool          RecvPost(CSession* pSession);              //WSARecv해서 수신 데이터 받는 함수
	bool          SendPost(CSession* pSession);              //WSASend해서 데이터 송신하는 함수
	bool          SessionInvalid(CSession* pSession, UINT64 SessionID);        //세션 유효성 체크
	bool          Release(CSession* pSession, UINT64 CheckID, long retIOCount);
	
	void          RecvIOProc(CSession* pSession, DWORD cbTransferred);
	void          SendIOProc(CSession* pSession, DWORD cbTransferred);
	void          ReleaseProc(CSession* pSession);

	void          Encoding(char* ptr, int len, UCHAR randkey); //해당 위치 부터 len만큼 인코딩 작업
	void          Decoding(char* ptr, int len, UCHAR randkey); //해당 위치 부터 len만큼 디코딩 작업 



};

