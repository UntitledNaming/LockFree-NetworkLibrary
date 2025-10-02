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

	//�α� ����
	int  m_iLogLevel;
	int  m_iSizeUpCnt;
	long m_iLogCount;

	size_t m_DEFAULT_LEN;
	size_t m_MAX_LEN;


	//Type�� ����ȭ ��ü �����ϱ� ���� �ڷᱸ��(���� Ÿ�Ե��� ���ÿ� �α� ������� �ڷ� ���� ��ȸ�� �� ������ SRWLOCK)
	CRITICAL_SECTION                           m_iMapLock;
	std::unordered_map<std::wstring, SRWLOCK*> m_iLockTable;

	//���� ���
	const WCHAR* directoryPath;

	//���� ���
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

//�ʱ�ȭ �۾�
static CLogClass* Log = CLogClass::GetInstance();


#define LOG(Type, LogLevel, format, ...) \
Log->GetInstance()->Log(Type,LogLevel,format,__VA_ARGS__)

#define LOGHEX(Type, LogLevel,Byte, Len ) \
Log->GetInstance()->LogHex(Type, LogLevel, Byte, Len)


#pragma once

