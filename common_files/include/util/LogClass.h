#pragma once

#define FILE_HEADER_LEN 512

enum en_LOG_LEVEL
{
	dfLOG_LEVEL_DEBUG = 0,
	dfLOG_LEVEL_ERROR = 1,
	dfLOG_LEVEL_SYSTEM = 2,
};

class CLogClass
{
private:

	//로그 레벨
	int  m_iLogLevel;
	int  m_iSizeUpCnt;
	long m_iLogCount;

	size_t m_DEFAULT_LEN;
	size_t m_MAX_LEN;


	//Type별 동기화 객체 관리하기 위한 자료구조(여러 타입들이 동시에 로그 남길려고 자료 구조 순회할 수 있으니 SRWLOCK)
	CRITICAL_SECTION                           m_iMapLock;
	std::unordered_map<std::wstring, SRWLOCK*> m_iLockTable;

	//폴더 경로
	const WCHAR* directoryPath;

	//이전 년월
	WORD oldMonth;
	WORD oldYear;

private:
	CLogClass();
	~CLogClass() {};

public:
	static CLogClass* GetInstance()
	{
		static CLogClass Log;

		return &Log;
	}

	void Init(int LogLevel);
	void ChangeLogLevel();
	void Log(const WCHAR* szType, en_LOG_LEVEL LogLevel, const WCHAR* szStringFormat, ...);
	void LogHex(const WCHAR* szType, en_LOG_LEVEL LogLevel, BYTE* pByte, int iByteLen);
};

//초기화 작업
static CLogClass* Log = CLogClass::GetInstance();


#define LOG(Type, LogLevel, format, ...) \
Log->GetInstance()->Log(Type,LogLevel,format,__VA_ARGS__)

#define LOGHEX(Type, LogLevel,Byte, Len ) \
Log->GetInstance()->LogHex(Type, LogLevel, Byte, Len)


#pragma once

