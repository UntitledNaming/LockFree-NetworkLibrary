#include <windows.h>
#include <unordered_map>
#include <string>
#include <strsafe.h>
#pragma warning(disable:4996)
#pragma comment(lib,"winmm.lib")
#define PROFILE
#include "ProfilerTLS.h"

CProfileTLS::CProfileTLS()
{
}

CProfileTLS::~CProfileTLS()
{
}

bool CProfileTLS::Init()
{
	//TlsIndex 저장
	m_tlsIdx = TlsAlloc();
	if (m_tlsIdx == TLS_OUT_OF_INDEXES)
	{
		wprintf(L"CProfilerManger_Init()_TlsAlloc Error : %d \n", GetLastError());
		return false;
	}

	//동기화 객체 초기화
	InitializeSRWLock(&m_arrayLock);

	m_arrayIdx = 0;

	return true;
}

void CProfileTLS::Destroy()
{
	for (int i = 0; i < m_arrayIdx; i++)
	{
		delete m_sampleArray[i].s_tablePtr;
		m_sampleArray[i].s_tablePtr = nullptr;
	}
}

bool CProfileTLS::ProfileInit()
{
	AcquireSRWLockExclusive(&m_arrayLock);

	if (m_arrayIdx >= MAX_THREAD_COUNT)
	{
		wprintf(L"ProfilerManager_ProfileInit()_Thread Count Max Error \n");
		ReleaseSRWLockExclusive(&m_arrayLock);
		return false;
	}

	m_sampleArray[m_arrayIdx].s_threadID = GetCurrentThreadId();
	m_sampleArray[m_arrayIdx].s_tablePtr = new std::unordered_map<std::wstring, st_PROFILE_DATA>;

	if (!TlsSetValue(m_tlsIdx, m_sampleArray[m_arrayIdx].s_tablePtr))
	{
		wprintf(L"ProfilerManager_ProfileInit()_TlsSetValue Error : %d \n", GetLastError());
		ReleaseSRWLockExclusive(&m_arrayLock);
		return false;
	}

	m_arrayIdx++;
	ReleaseSRWLockExclusive(&m_arrayLock);

	return true;
}

void CProfileTLS::SaveProfilingData(WCHAR* szName)
{
	errno_t err;
	FILE* fp;
	SYSTEMTIME    stNowTime;


	//현재 날짜와 시간 얻기
	WCHAR filename[MAX_PATH];
	GetLocalTime(&stNowTime);
	wsprintf(filename, L"Profiling [%s]_%d%02d%02d_%02d.%02d.%02d.txt", szName, stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);

	//=======================================================================================================
	// 
	//각 스레드 TLS에 저장된 샘플링 자료구조 순회하면서 Tagging 데이터 Copy
	// 
	//=======================================================================================================
	PROFILEDATA*     ptr[MAX_THREAD_COUNT]; // 원소 : 배열의 첫 주소
	size_t           size;
	INT              j = 0;
	for (int i = 0; i < m_arrayIdx; i++)
	{
		size = m_sampleArray[i].s_tablePtr->size();

		//해시 테이블 size 만큼 st_PROFILE_DATA 배열 생성해서 그 포인터를 ptr[i]에 저장
		ptr[i] = new PROFILEDATA[size];

		std::unordered_map<std::wstring, PROFILEDATA>::iterator it;
		j = 0;

		//한 스레드의 해시 테이블을 iterator로 순회하면서 저장된 value를 ptr[i][j]에 있는 st_PROFILE_DATA 원소에 Copy
		for (it = m_sampleArray[i].s_tablePtr->begin(); it != m_sampleArray[i].s_tablePtr->end(); ++it)
		{
			ptr[i][j].s_callCnt = it->second.s_callCnt;
			ptr[i][j].s_totalTime = it->second.s_totalTime;
			ptr[i][j].s_name = it->second.s_name;

			for (int k = 0; k < 2; k++)
			{
				ptr[i][j].s_maxTime[k] = it->second.s_maxTime[k];
				ptr[i][j].s_minTime[k] = it->second.s_minTime[k];
			}

			j++;
		}
	}

	//=======================================================================================================
	// 
	//각 스레드 별 샘플링 데이터 계산 및 출력, 취합 출력
	// 
	//=======================================================================================================


	const WCHAR* pHeader[3];
	for (int i = 0; i < 3; i++)
	{
		if (i == 1)
		{
			pHeader[i] = L"                                     Name |                     Average  |                            Min  |                            Max  |       Call  |\n";
			continue;
		}
		pHeader[i] = L"--------------------------------------------------------------------------------------------------------------------------------------------------------------\n";
	}

	//각 스레드 해시 테이블 size 합산
	size_t totalsize = 0;
	size_t maxsize = 0;
	for (int i = 0; i < m_arrayIdx; i++)
	{
		maxsize = max(maxsize, m_sampleArray[i].s_tablePtr->size());
		totalsize += m_sampleArray[i].s_tablePtr->size();
	}


	//Total 합산을 위한 배열 생성 및 Tag 초기화 작업
	st_TOTAL_DATA* pTotal = new st_TOTAL_DATA[totalsize];
	for (int i = 0; i < totalsize; i++)
	{
		pTotal[i].s_useFlag = 0;
		pTotal[i].s_callCnt = 0;
	}


	// 계산된 정보를 파일에 저장할 버퍼에 Copy(헤더도 이때 저장)
	// 프로파일러 헤더 및 데이터 저장
	// MAX_STRING_SIZE : 파일에 저장할 정보들 문자열 저장시 1줄 최대 크기
	size_t reMain = (MAX_HEADER_SIZE + (MAX_STRING_SIZE) * (totalsize + maxsize + m_arrayIdx)) * sizeof(WCHAR);
	WCHAR* pDataBuffer = (WCHAR*)malloc(reMain);
	WCHAR* pEnd = nullptr;
	HRESULT ret;

	memset(pDataBuffer, NULL, reMain);

	// 1. 헤더 저장
	//3번째 인자 : 문자열 Copy후 문자열 끝 NULL 가리키는 포인터
	//4번째 인자 : Dest 버퍼에 문자열 Copy후 남은 크기
	ret = StringCchPrintfExW(pDataBuffer, reMain, &pEnd, &reMain, 0, pHeader[0]);
	if (ret != S_OK)
	{
		__debugbreak();
	}

	for (int i = 1; i < 3; i++)
	{
		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, pHeader[i]);
		if (ret != S_OK)
		{
			__debugbreak();
		}

	}


	// 2. 데이터 계산 및 버퍼에 저장
	size_t tableSize = 0;
	DOUBLE avgTime;
	DOUBLE maxTime;
	DOUBLE minTime;
	DWORD  threadID;
	DWORD  realSize = 0;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	//스레드 별 샘플링 데이터 버퍼에 Copy
	for (int i = 0; i < m_arrayIdx; i++)
	{
		//해시 테이블 값 저장한 배열 순회
		tableSize = m_sampleArray[i].s_tablePtr->size();
		threadID = m_sampleArray[i].s_threadID;

		for (int j = 0; j < tableSize; j++)
		{
			//전체 TotalTick에서 Max, Min 뺀 값을 call 횟수로 나눔
			maxTime = static_cast<DOUBLE>((ptr[i][j].s_maxTime[0] + ptr[i][j].s_maxTime[1])) / 2 * 1000000 / freq.QuadPart;
			minTime = static_cast<DOUBLE>((ptr[i][j].s_minTime[0] + ptr[i][j].s_minTime[1])) / 2 * 1000000 / freq.QuadPart;
			if (ptr[i][j].s_callCnt <= 4)
			{
				avgTime = static_cast<DOUBLE>(ptr[i][j].s_totalTime / ptr[i][j].s_callCnt) * 1000000 / freq.QuadPart;
			}
			else
				avgTime = ((static_cast<DOUBLE>((ptr[i][j].s_totalTime - ptr[i][j].s_maxTime[0] - ptr[i][j].s_maxTime[1] - ptr[i][j].s_minTime[0] - ptr[i][j].s_minTime[1])) / (ptr[i][j].s_callCnt - 4)) * (double)1000000 / freq.QuadPart);

			ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"Thread ID : %d |         %s |          %4lf ㎲ |          %4lf ㎲ |           %4lf ㎲ |      %lld | \n", threadID, ptr[i][j].s_name.c_str(), avgTime, minTime, maxTime, ptr[i][j].s_callCnt);
			if (ret != S_OK)
			{
				__debugbreak();
			}

			//Total배열 순회 하면서 같은 Tag있으면 더해주기
			int k;
			for (k = 0; k < totalsize; k++)
			{
				//같은 Tag를 가질때
				if (pTotal[k].s_name == ptr[i][j].s_name)
				{
					pTotal[k].s_totalCallTime += ptr[i][j].s_callCnt;
					pTotal[k].s_totalAvgTime += avgTime;
					pTotal[k].s_totalMaxTime += maxTime;
					pTotal[k].s_totalMinTime += minTime;
					pTotal[k].s_useFlag = 1;
					pTotal[k].s_callCnt++;
					break;
				}

			}


			//만약 ptr[i][j]에 저장된 st_PROFILE_DATA의 Tag를 가지고 Total배열 반복물 다 돌아도 같은 Tag를 못찾았으면 k는 totalsize값일 것이고 total 배열에 공간 할당해서 데이터 저장해줘야 함.
			if (k == totalsize)
			{
				//다시 Total 배열 돌면서 사용안하는 공간 찾기
				for (int l = 0; l < totalsize; l++)
				{
					//사용 안하는 공간이라면 
					if (pTotal[l].s_useFlag == 0)
					{
						pTotal[l].s_name = ptr[i][j].s_name;
						pTotal[l].s_totalCallTime = ptr[i][j].s_callCnt;
						pTotal[l].s_totalAvgTime = avgTime;
						pTotal[l].s_totalMaxTime = maxTime;
						pTotal[l].s_totalMinTime = minTime;
						pTotal[l].s_useFlag = 1;
						pTotal[l].s_callCnt = 1;
						break;
					}
				}

			}

		}

		//한 스레드 샘플 데이터 Copy했으면 ----- 만들기
		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"--------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
		if (ret != S_OK)
		{
			__debugbreak();
		}
	}

	//Total 배열에 있는 값 출력하기
	INT cnt;
	for (int i = 0; i < totalsize; i++)
	{
		//사용안하는 배열이면 Pass
		if (pTotal[i].s_useFlag == 0)
			continue;

		cnt = pTotal[i].s_callCnt;

		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"         Total Data   |         %s |          %4lf ㎲ |          %4lf ㎲ |           %4lf ㎲ |      %lld | \n", pTotal[i].s_name.c_str(), pTotal[i].s_totalAvgTime / cnt, pTotal[i].s_totalMinTime / cnt, pTotal[i].s_totalMaxTime / cnt, pTotal[i].s_totalCallTime);
		if (ret != S_OK)
		{
			__debugbreak();
		}
	}



	// 3. 파일 스트림 생성
	err = _wfopen_s(&fp, filename, L"wb");
	if (err != 0)
	{
		wprintf(L"파일 열기 실패. 에러 코드: %d \n", err);
		__debugbreak();
		return;
	}


	//저장
	fwrite(pDataBuffer, (MAX_HEADER_SIZE + (MAX_STRING_SIZE) * (totalsize + maxsize + m_arrayIdx))*sizeof(WCHAR), 1, fp);


	// 4. 스트림 닫기 및 뒷 정리
	fclose(fp);

	for (int i = 0; i < m_arrayIdx; i++)
	{
		delete[] ptr[i];
		ptr[i] = nullptr;
	}
	delete[] pTotal;
	pTotal = nullptr;

	free(pDataBuffer);
	pDataBuffer = nullptr;
}

CProfileTLS::CProfiler::CProfiler(const WCHAR* tag)
{
	PROFILE_MAP_PTR ret;
	ret = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->m_tlsIdx);
	if (ret == nullptr)
	{
		if (!CProfileTLS::GetInstance()->ProfileInit())
			__debugbreak();
	}


	m_tag = const_cast<WCHAR*>(tag);
	PRO_BEGIN(const_cast<WCHAR*>(tag));
}

CProfileTLS::CProfiler::~CProfiler()
{
	PRO_END(const_cast<WCHAR*>(m_tag));
}

void CProfileTLS::CProfiler::ProfileBegin(WCHAR* szName)
{
	std::wstring str = szName;


	if (str.size() >= MAX_STRING_SIZE)
		__debugbreak();

	//스레드 샘플링 데이터 자료구조에 접근
	PROFILE_MAP_PTR pTable;
	pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->GetTlsIndex());

	std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = pTable->find(str);


	// wstring str을 key로 할때 해시 테이블에 해당 key가 없는 경우 초기값 세팅 후 테이블에 저장
	if (it == pTable->end())
	{
		m_sampleData.s_callCnt = 0;
		m_sampleData.s_totalTime = 0;
		m_sampleData.s_errorFlag = 0;
		m_sampleData.s_name = str;

		for (int i = 0; i < 2; i++)
		{
			m_sampleData.s_maxTime[i] = 0;
			m_sampleData.s_minTime[i] = ULLONG_MAX;
		}

	}

	else
	{
		//해시 테이블에 있으면 그 값으로 SampleData 세팅
		m_sampleData.s_callCnt = it->second.s_callCnt;
		m_sampleData.s_errorFlag = it->second.s_errorFlag;
		m_sampleData.s_totalTime = it->second.s_totalTime;
		m_sampleData.s_name = it->second.s_name;

		for (int i = 0; i < 2; i++)
		{
			m_sampleData.s_maxTime[i] = it->second.s_maxTime[i];
			m_sampleData.s_minTime[i] = it->second.s_minTime[i];
		}
	}


	//ErrorFlag가 0이거나 End에서 설정한 플래그 값인 0x0010이 아니면 Error를 발생시킴
	if (m_sampleData.s_errorFlag != 0 && m_sampleData.s_errorFlag != 0x0010)
		throw szName;


	//정상적으로 세팅 되었으면 Begin 제대로 했으니 ErrorFlag 세팅
	m_sampleData.s_errorFlag = 0xFFFF;

	m_threadID = GetCurrentThreadId();

	//시간 측정 시작
	QueryPerformanceCounter(&m_sampleData.s_startTick);
}

void CProfileTLS::CProfiler::ProfileEnd(WCHAR* szName)
{
	//종료 시간 갱신
	LARGE_INTEGER endTick;
	LARGE_INTEGER freq;
	uint64_t DiffTick;
	DWORD id;

	QueryPerformanceCounter(&endTick);
	QueryPerformanceFrequency(&freq);
	id = GetCurrentThreadId();
	if (m_threadID != id)
		__debugbreak();

	//생성자에서 설정한 멤버 변수 에러 플래그랑 다르면 에러
	if (m_sampleData.s_errorFlag != 0xFFFF)
		throw szName;

	//Total에 합산
	DiffTick = (endTick.QuadPart - m_sampleData.s_startTick.QuadPart);
	m_sampleData.s_totalTime += DiffTick;

	//MAX값 갱신
	if (m_sampleData.s_maxTime[0] < DiffTick)
	{
		uint64_t temp = m_sampleData.s_maxTime[0];
		m_sampleData.s_maxTime[0] = DiffTick;
		m_sampleData.s_maxTime[1] = temp;
	}

	//MIN값 갱신
	if (m_sampleData.s_minTime[0] > DiffTick)
	{
		uint64_t temp = m_sampleData.s_minTime[0];
		m_sampleData.s_minTime[0] = DiffTick;
		m_sampleData.s_minTime[1] = temp;
	}

	//호출량 증가
	m_sampleData.s_callCnt++;

	//에러 플래그 설정
	m_sampleData.s_errorFlag = 0x0010;


	//멤버 변수에 저장된 값 갱신 후 해시 테이블에 저장
	PROFILE_MAP_PTR pTable;
	pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->GetTlsIndex());

	std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = pTable->find(m_sampleData.s_name);

	//새로운 Tag일때
	if (it == pTable->end())
	{
		pTable->insert(std::pair<std::wstring, st_PROFILE_DATA>(m_sampleData.s_name, m_sampleData));
		return;
	}

	//이미 있으면
	it->second = m_sampleData;
	return;
}
