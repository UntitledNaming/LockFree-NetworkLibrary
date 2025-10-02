
#include <iostream>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <conio.h>
#include <strsafe.h>
#include <unordered_map>
#include "LogClass.h"


CLogClass::CLogClass()
{
	//멤버 변수 초기화
	m_iLogLevel = dfLOG_LEVEL_ERROR;
	m_iSizeUpCnt = 0;
	m_iLogCount = 0;
	m_DEFAULT_LEN = 100;
	m_MAX_LEN = 1000;
	oldMonth = NULL;
	oldYear = NULL;
	directoryPath = L"C:\\Users\\Public\\LogFolder";
	InitializeCriticalSection(&m_iMapLock);

	//디렉토리 생성
	if (!CreateDirectory(directoryPath, NULL))
	{
		if (GetLastError() != 183)
			__debugbreak();
	}

	//이전 년월 저장 및 디렉토리 생성
	SYSTEMTIME    stNowTime;
	GetLocalTime(&stNowTime);

	WCHAR directoryBuf[MAX_PATH];
	wsprintf(directoryBuf, L"%s\\%04d%02d", directoryPath, stNowTime.wYear, stNowTime.wMonth);

	//디렉토리 생성 성공하면 old값 갱신
	if (CreateDirectoryW(directoryBuf, NULL))
	{
		oldMonth = stNowTime.wMonth;
		oldYear = stNowTime.wYear;
	}
	else
	{
		if (GetLastError() != 183)
			__debugbreak();
		
	}
}

void CLogClass::Init(int LogLevel)
{
	m_iLogLevel = LogLevel;
}

void CLogClass::ChangeLogLevel()
{
	system("cls");
	std::cout << "==============================================" << std::endl;
	std::cout << "dfLOG_LEVEL_DEBUG  :  0 " << std::endl;
	std::cout << "dfLOG_LEVEL_ERROR  :  1 " << std::endl;
	std::cout << "dfLOG_LEVEL_SYSTEM :  2 " << std::endl;
	std::cout << "==============================================" << std::endl;
	std::cout << "Log Level : ";
	std::cin >> m_iLogLevel;
}

void CLogClass::Log(const WCHAR* szType, en_LOG_LEVEL LogLevel, const WCHAR* szStringFormat, ...)
{
	//로그 호출 시 설정한 레벨이 저장할 로그 레벨 보다 낮으면 저장안함.
	if (LogLevel < m_iLogLevel)
		return;

	//형식 지정자 매개인자 저장하기
	va_list args;
	WCHAR* pBuffer = (WCHAR*)malloc(m_DEFAULT_LEN * sizeof(WCHAR));
	HRESULT ret;

	bool b_reSize = false;

	while (1)
	{
		va_start(args, szStringFormat);
		ret = StringCchVPrintfW(pBuffer, m_DEFAULT_LEN - 2, szStringFormat, args);
		va_end(args);

		if (SUCCEEDED(ret))
		{
			m_iSizeUpCnt = 0;
			break;
		}

		//버퍼 부족할 때(만약 계속 버퍼 늘리는데도 버퍼 부족한거면 Crash)
		else if (ret == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			if (m_iSizeUpCnt == 5)
				__debugbreak();

			free(pBuffer);

			m_DEFAULT_LEN = m_DEFAULT_LEN * 2;
			pBuffer = (WCHAR*)malloc(m_DEFAULT_LEN * sizeof(WCHAR));

			b_reSize = true;
		}
	}


	errno_t err;
	FILE* fp;

	SRWLOCK* pFileLock;

	//여기서 Type 별로 Lock 거는 작업 진행.
	std::wstring key(szType); //인자로 들어온 문자열 복사해서 key에 저장하기

	//해당 key를 map에서 찾기
	EnterCriticalSection(&m_iMapLock);
	std::unordered_map<std::wstring, SRWLOCK*>::iterator it;
	it = m_iLockTable.find(key);
	if (it != m_iLockTable.end())
	{
		//key를 찾았을 때
		pFileLock = it->second;
		//Type에 Lock 걸기
		AcquireSRWLockExclusive(pFileLock);
	}
	else
	{
		//key를 못찾았을 때 table 에 key와 SRWLOCK 추가하기
		pFileLock = new SRWLOCK;

		//Lock 초기화
		InitializeSRWLock(pFileLock);

		//key와 value 저장
		m_iLockTable.insert(std::pair<std::wstring, SRWLOCK*>(key, pFileLock));


		//Type에 Lock 걸기
		AcquireSRWLockExclusive(pFileLock);
	}

	//버퍼 키웠거나 잘 받았으면 이제 파일에 저장해야 함.
	SYSTEMTIME    stNowTime;
	GetLocalTime(&stNowTime);

	//파일 이름
	WCHAR filename[MAX_PATH];


	//이전 년월과 현재 년월 다르면 디렉토리 생성
	if (oldMonth != stNowTime.wMonth)
	{
		WCHAR directoryBuf[MAX_PATH];
		wsprintf(directoryBuf, L"%s\\%04d%02d", directoryPath, stNowTime.wYear, stNowTime.wMonth);

		//디렉토리 생성 성공하면 old값 갱신
		if (CreateDirectory(directoryBuf, NULL))
		{
			oldMonth = stNowTime.wMonth;
			oldYear = stNowTime.wYear;
		}

	}
	LeaveCriticalSection(&m_iMapLock);


	//Type 별로 스트림 생성
	wsprintf(filename, L"%s\\%04d%02d\\%04d%02d_%s.txt", directoryPath, stNowTime.wYear, stNowTime.wMonth, stNowTime.wYear, stNowTime.wMonth, szType);
	err = _wfopen_s(&fp, filename, L"a+"); //파일 없으면 새로 생성하고 파일 있으면 파일 끝에 데이터 덮어 쓰기 위한 파일 스트림 생성
	if (err != 0)
	{
		wprintf(L"파일 오픈 실패 \n");
		ReleaseSRWLockExclusive(pFileLock);
		return;
	}


	//파일에 저장할 내용 Header에 넣기
	WCHAR Header[FILE_HEADER_LEN];
	int len;
	if (LogLevel == dfLOG_LEVEL_DEBUG)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"[%s] [%04d-%02d-%02d %02d:%02d:%02d / DEBUG / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}

	else if (LogLevel == dfLOG_LEVEL_ERROR)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"[%s] [%04d-%02d-%02d %02d:%02d:%02d / ERROR / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}

	else if (LogLevel == dfLOG_LEVEL_SYSTEM)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"[%s] [%04d-%02d-%02d %02d:%02d:%02d / SYSTEM / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}


	fwprintf(fp, L"\n");

	//Header 내용 저장
	fwprintf(fp, Header);

	//LOG 가변인자로 들어온 문자열 저장
	fwprintf(fp, pBuffer);


	//만약 로그 버퍼 키웠으면 이에 대한 로그도 남기기
	if (b_reSize == true)
	{
		const WCHAR* p = L"LogBufferResize";
		fwprintf(fp, L"\n");
		fwprintf(fp, Header);
		fwprintf(fp, p);
	}


	fclose(fp);
	free(pBuffer);


	ReleaseSRWLockExclusive(pFileLock);
}

void CLogClass::LogHex(const WCHAR* szType, en_LOG_LEVEL LogLevel, BYTE* pByte, int iByteLen)
{
	//로그 호출 시 설정한 레벨이 저장할 로그 레벨 보다 낮으면 저장안함.
	if (LogLevel < m_iLogLevel)
		return;


	errno_t err;
	FILE* fp;
	SRWLOCK* pFileLock;

	//여기서 Type 별로 Lock 거는 작업 진행.
	std::wstring key(szType); //인자로 들어온 문자열 복사해서 key에 저장하기

	//해당 key를 map에서 찾기
	EnterCriticalSection(&m_iMapLock);
	std::unordered_map<std::wstring, SRWLOCK*>::iterator it;
	it = m_iLockTable.find(key);
	if (it != m_iLockTable.end())
	{
		//key를 찾았을 때
		pFileLock = it->second;
		//Type에 Lock 걸기
		AcquireSRWLockExclusive(pFileLock);
	}
	else
	{
		//key를 못찾았을 때 table 에 key와 SRWLOCK 추가하기
		pFileLock = new SRWLOCK;

		//Lock 초기화
		InitializeSRWLock(pFileLock);

		//key와 value 저장
		m_iLockTable.insert(std::pair<std::wstring, SRWLOCK*>(key, pFileLock));


		//Type에 Lock 걸기
		AcquireSRWLockExclusive(pFileLock);
	}


	//버퍼 키웠거나 잘 받았으면 이제 파일에 저장해야 함.
	SYSTEMTIME    stNowTime;
	GetLocalTime(&stNowTime);

	//파일 이름
	WCHAR filename[MAX_PATH];


	//이전 년월과 현재 년월 다르면 디렉토리 생성
	if (oldMonth != stNowTime.wMonth)
	{
		WCHAR directoryBuf[MAX_PATH];
		wsprintf(directoryBuf, L"%s\\%04d%02d", directoryPath, stNowTime.wYear, stNowTime.wMonth);

		//디렉토리 생성 성공하면 old값 갱신
		if (CreateDirectory(directoryBuf, NULL))
		{
			oldMonth = stNowTime.wMonth;
			oldYear = stNowTime.wYear;
		}

	}

	LeaveCriticalSection(&m_iMapLock);


	//Type 별로 스트림 생성
	wsprintf(filename, L"%s\\%04d%02d\\%04d%02d_%s.txt", directoryPath, stNowTime.wYear, stNowTime.wMonth, stNowTime.wYear, stNowTime.wMonth, szType);
	err = _wfopen_s(&fp, filename, L"a+"); //파일 없으면 새로 생성하고 파일 있으면 파일 끝에 데이터 덮어 쓰기 위한 파일 스트림 생성
	if (err != 0)
	{
		wprintf(L"파일 오픈 실패 \n");
		ReleaseSRWLockExclusive(pFileLock);
		return;
	}


	//파일에 저장할 내용 Header에 넣기
	WCHAR Header[FILE_HEADER_LEN];
	int len;

	//아래 swprintf안쓰고 그냥 바로 fwprintf로 저장가능함.(todo)
	if (LogLevel == dfLOG_LEVEL_DEBUG)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"\n[%s] [%04d-%02d-%02d %02d:%02d:%02d / DEBUG / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}

	else if (LogLevel == dfLOG_LEVEL_ERROR)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"\n[%s] [%04d-%02d-%02d %02d:%02d:%02d / ERROR / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}

	else if (LogLevel == dfLOG_LEVEL_SYSTEM)
	{
		len = swprintf(Header, sizeof(Header) / sizeof(WCHAR), L"\n[%s] [%04d-%02d-%02d %02d:%02d:%02d / SYSTEM / %9d] ", szType, stNowTime.wYear, stNowTime.wMonth,
			stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, InterlockedIncrement(&m_iLogCount));
	}

	//Header 내용 저장
	fwprintf(fp, Header);

	for (int i = 0; i < iByteLen; i++)
		fwprintf(fp, L"%02x", pByte[i]);


	fclose(fp);
	ReleaseSRWLockExclusive(pFileLock);
}

