#pragma once

#define df_GAMELIB_MAX_THREAD_COUNT     100
#define df_GAMELIB_DEFAULT_GROUP_COUNT  100
#define df_GAMELIB_IP_LEN               16
#define df_GAMELIB_INDEX_POS            48
#define df_GAMELIB_ID_POS               47
#define df_SENDQ_MAX_SIZE               10000

class CGroup;
class CSession;
class CMessage;
class IUser;

template <typename T>
class CMemoryPool;

template <typename T>
class LFStack;


class CGameLibrary
{
public:
	enum TASK_TYPE
	{
		en_RELEASE,       // GQCS Key : CSession*
		en_FRAME,         // GQCS Key : GroupID
		en_GROUPMOVE,     // GQCS Key : CMessage* (sessionID, IUser*)
	};


public:
	struct st_IOFLAG
	{
		long long s_cnt;
		long long s_flag;
	};

private:
	HANDLE                                   m_IOCP;                                          // IOCP 리소스 핸들
	SOCKET                                   m_Listen;                                        // 서버 리슨 소켓
	BOOL                                     m_Endflag;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Config 정보
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::string                              m_IP;                                            // 서버 IP
	INT                                      m_Port;                                          // 서버 Port
	INT                                      m_MaxSessionCnt;                                 // 최대 세션 갯수 설정
	INT                                      m_CreateWorkerCnt;                               // 생성 워커 스레드 갯수
	INT                                      m_ConcurrentCnt;                                 // IOCP 러닝 스레드 갯수
	INT                                      m_SendFrame;                                     // Send 스레드 Sleep값
	INT                                      m_PacketCode;                                    // 네트워크 라이브러리 패킷 코드
	INT                                      m_FixedKey;                                      // 인코딩 고정 키 값
	INT                                      m_SendThFL;                                      // Send 스레드 생성 버전 플래그 ( 0 : 끄기 / 1 : 켜기 )
	INT                                      m_Nagle;                                         // 네이글 옵션
																	                          

public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 모니터링 변수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	LONG                                     m_AcceptTPS;                                     // 초당 Accept 처리 횟수 모니터링 용
	LONG                                     m_RecvIOTPS;                                     // 초당 Recv 처리 횟수 모니터링 용
	LONG                                     m_SendIOTPS;                                     // 초당 Send 처리 횟수 모니터링 용
	INT64                                    m_AcceptTotal;                                   // Accept처리한 최대 세션 갯수
	SHORT                                    m_CurSessionCnt;                                 // 세션 배열에서 실제로 사용중인 세션 갯수


private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 스레드 정보 및 자료구조
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::thread                              m_IOCPWorkerThread[df_GAMELIB_MAX_THREAD_COUNT]; // Worker 관리 배열
	std::thread                              m_AcceptThread;                                  // AcceptThread 관리
	std::thread                              m_SendThread;                                    // SendThread
	std::thread                              m_FrameThread;                                   // 프레임 체크할 스레드


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 세션 관리 자료구조 및 멤버 변수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	CSession*                                m_SessionTable;      	                          // 세션 구조체를 관리할 배열
	LFStack<UINT16>*                         m_pSessionIdxStack;                              // 세션 배열의 index 관리할 자료구조
	UINT64                                   m_AllocID;                                       // 세션에게 할당한 세션 ID



	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 그룹 관련 변수 및 자료구조
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<CGroup*>                     m_GroupArray;                                    // 컨텐츠 객체 관리할 자료구조
	std::unordered_map<std::wstring, UINT16> m_GroupMap;                                      // Attach 인자로 받은 문자열과 그룹 객체 배열 index 매핑 자료구조
	UINT16                                   m_GroupID;                                       // 그룹 등록시 부여할 ID (위 자료구조의 Index로 사용)


public:
	CGameLibrary();
	~CGameLibrary();

	bool          Run();                                                                      // 게임 라이브러리 작동 함수
	void          Stop();                                                                     // 게임 라이브러리 멤버 정리 및 스레드 정리
		          																	          
	bool          Attach(CGroup* pContents, std::wstring contentsType, UINT64 groupframe, BOOL shared = FALSE);          // 그룹관리 벡터에 등록할 함수

private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 초기화, 스레드 생성, 제거 함수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          Mem_Init(INT sessionmax, INT createiothread, INT activethread, WORD packet_code, WORD packet_key, INT sendframe, INT sendflag, INT nagle);  // 멤버 초기화
	void          Net_Init(WCHAR* serverIp, INT serverport);           // 네트워크  초기화
	void          Session_Init();
	void          Group_Init();
	void          Monitoring_Init();
		          
	void          Thread_Create(INT sendflag);
	void          Thread_Destroy();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 게임 라이브러리 스레드
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          FrameThread();                                                              // 그룹 객체 프레임 로직 PQCS로 쏘기 위한 스레드
	void          WorkerThread();                                                             // Task 및 네트워크 처리 위한 스레드 
	void          SendThread();                                                               // Send 담당 스레드
	void          AcceptThread();                                                             // Accept 스레드


public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 외부 제공 함수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	bool          SendPacket(UINT64 SessionID, CMessage* pMessage);
	bool          Disconnect(UINT64 SessionID);
	bool          FindIP(UINT64 SessionID, std::wstring& OutIP);
	bool          GroupMove(std::wstring ToContents, UINT64 sessionID, IUser* pUser);
		          
	LONG          GetAcceptTPS();
	LONG          GetRecvIOTPS();
	LONG          GetSendIOTPS();
	INT64         GetAcceptTotal();
	SHORT         GetCurSessionCount();
		          
	void          SetAcceptTPS(LONG value);
	void          SetRecvIOTPS(LONG value);
	void          SetSendIOTPS(LONG value);
			      
	CGroup*       GetGroupPtr(std::wstring Contents);

private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 게임라이브러리 네트워크 내부 처리 함수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	bool          RecvPost(CSession* pSession);
	bool          SendPost(CSession* pSession);
	bool          Release(CSession* pSession,long long retIOCount);
	bool          SessionInvalid(CSession* pSession, UINT64 CheckID);                         // 세션 유효성 체크 함수(true면 Count 하나 올림)
																	                          
	void          RecvIOProc(CSession* pSession, DWORD cbTransferred);                        
	void          SendIOProc(CSession* pSession, DWORD cbTransferred);                        
	void          Encoding(char* ptr, int len, UCHAR randkey);                                //해당 위치 부터 len만큼 인코딩 작업
	void          Decoding(char* ptr, int len, UCHAR randkey);                                //해당 위치 부터 len만큼 디코딩 작업 
	void          FindSession(UINT64 sessionID, CSession** ppSession);                        // 단순히 세션ID로 세션 배열의 세션 찾는 함수, 아웃파라미터 nullptr이면 세션ID가 INVALID
	UINT64        MakeSessionID(UINT16 index, UINT64 allocID);




private:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// IOCP Worker 스레드 PQCS 처리 함수
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void          ReleaseProc(CSession* pSession);
	void          FrameProc(UINT16 groupID);
	void          GroupMoveProc(CMessage* pMessage);
};

