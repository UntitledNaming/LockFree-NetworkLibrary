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
	// �������Ϸ� ���ø� ������ ����ü
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
		INT                 s_callCnt; //�����ٶ� ���� ������Ű��
	};

private:
	CProfileTLS();
	~CProfileTLS();

	/////////////////////////////////////////////////////////////////////////////
    // ��ü ���ø� ������ �ڷᱸ���� Index�� ���ϰ� TlsIndex�� ����
    // 
    // Parameters: ����
    // Return: ��ü ���ø� ������ �迭�� ���� �����ϸ� ���� �ȵǸ� ����
    /////////////////////////////////////////////////////////////////////////////
	bool ProfileInit();

public:
	/////////////////////////////////////////////////////////////////////////////
	// �������Ϸ��� ���� TlsIndex ���� �� ����ȭ ��ü �ʱ�ȭ
	// 
	// Parameters: ����
	// Return: TlsAlloc ����(true), ����(false)
	/////////////////////////////////////////////////////////////////////////////
	bool Init();

	/////////////////////////////////////////////////////////////////////////////
    // TLS index�� �����Ҵ� �ؼ� ������ �ؽ� ���̺� �ڷᱸ�� �Ҵ� �����ϱ�
    // Return: 
    /////////////////////////////////////////////////////////////////////////////
	void Destroy();

	/////////////////////////////////////////////////////////////////////////////
    // Profiling �� ����Ÿ�� Text ���Ϸ� ����Ѵ�.
    //
    // Parameters: (char *)��µ� ���� �̸�.
    // Return: ����.
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
		// ��� ����
		WCHAR*             m_tag;               //�����ڿ��� ���޵� Tag ���ڿ� ������
		PROFILEDATA        m_sampleData;        //Begin, End���� ����� ���ø� ������ ���� ����
		DWORD              m_threadID;
	};
};

