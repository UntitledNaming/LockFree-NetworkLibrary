#pragma once

#ifdef PROFILE
#define PRO_BEGIN(TagName) ProfileBegin(TagName)
#define PRO_END(TagName) ProfileEnd(TagName)

#else
#define PRO_BEGIN(TagName)
#define PRO_END(TagName)

#endif

#define MAX_THREAD_COUNT 50
#define MAX_HEADER_SIZE 512
#define MAX_STRING_SIZE  320

class CProfileTLS
{
private:
	// 프로파일러 샘플링 데이터 구조체
	struct st_PROFILE_DATA
	{
		UINT16                 s_errorFlag;
		std::wstring           s_name;
		LARGE_INTEGER          s_startTick;
		UINT64                 s_totalTime;
		UINT64                 s_minTime[2];
		UINT64                 s_maxTime[2];
		UINT64                 s_callCnt;
	}typedef PROFILEDATA;

	typedef std::unordered_map<std::wstring, PROFILEDATA>* PROFILE_MAP_PTR;
	typedef double DOUBLE;

	struct st_THREAD_PROFILE_TABLE
	{
		PROFILE_MAP_PTR        s_tablePtr;
		DWORD                  s_threadID;
	}typedef PROFILETABLE;

	struct st_TOTAL_DATA
	{
		BOOL                s_useFlag;
		std::wstring        s_name;
		DOUBLE              s_totalAvgTime;
		DOUBLE              s_totalMaxTime;
		DOUBLE              s_totalMinTime;
		UINT64              s_totalCallTime;
		INT                 s_callCnt; //더해줄때 마다 증가시키기
	};

private:
	CProfileTLS();
	~CProfileTLS();

	/////////////////////////////////////////////////////////////////////////////
    // 전체 샘플링 데이터 자료구조의 Index를 구하고 TlsIndex에 저장
    // 
    // Parameters: 없음
    // Return: 전체 샘플링 데이터 배열에 세팅 가능하면 성공 안되면 실패
    /////////////////////////////////////////////////////////////////////////////
	bool ProfileInit();

public:
	/////////////////////////////////////////////////////////////////////////////
	// 프로파일러를 위한 TlsIndex 저장 및 동기화 객체 초기화
	// 
	// Parameters: 없음
	// Return: TlsAlloc 성공(true), 실패(false)
	/////////////////////////////////////////////////////////////////////////////
	bool Init();

	/////////////////////////////////////////////////////////////////////////////
    // TLS index에 동적할당 해서 저장한 해시 테이블 자료구조 할당 해제하기
    // Return: 
    /////////////////////////////////////////////////////////////////////////////
	void Destroy();

	/////////////////////////////////////////////////////////////////////////////
    // Profiling 된 데이타를 Text 파일로 출력한다.
    //
    // Parameters: (char *)출력될 파일 이름.
    // Return: 없음.
    /////////////////////////////////////////////////////////////////////////////
	void SaveProfilingData(WCHAR* szName);

	inline        UINT16           GetTlsIndex()
	{
		return m_tlsIdx;
	}

	inline static CProfileTLS*     GetInstance()
	{
		static CProfileTLS Cpm;
		return &Cpm;
	}


private:
	DWORD                        m_tlsIdx;                                   
	UINT16                       m_arrayIdx;
	SRWLOCK                      m_arrayLock;
	PROFILETABLE                 m_sampleArray[MAX_THREAD_COUNT];


public:
	/////////////////////////////////////////////////////////////////////////////
	// Profile Sub Class
	/////////////////////////////////////////////////////////////////////////////
	class CProfiler
	{
	public:
		CProfiler(const WCHAR* tag);
		~CProfiler();

	private:
		void ProfileBegin(WCHAR* szName);
		void ProfileEnd(WCHAR* szName);

	private:
		// 멤버 변수
		WCHAR*             m_tag;               //생성자에서 전달된 Tag 문자열 포인터
		PROFILEDATA        m_sampleData;        //Begin, End에서 사용할 샘플링 데이터 담을 변수
		DWORD              m_threadID;
	};
};

