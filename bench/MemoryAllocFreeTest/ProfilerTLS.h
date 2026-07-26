#pragma once

#include <windows.h>
#include <unordered_map>
#include <string.h>
#include <strsafe.h>
#pragma warning(disable:4996)
#pragma comment(lib,"winmm.lib")

#ifdef PROFILE
#define PRO_BEGIN(TagName) ProfileBegin(TagName)
#define PRO_END(TagName) ProfileEnd(TagName)

#else
#define PRO_BEGIN(TagName)
#define PRO_END(TagName)

#endif

#define MAX_THREAD_COUNT 30
#define MAX_HEADER_SIZE 512
#define MAX_STRING_SIZE  320

class CProfilerManager
{
private:

	// 프로파일러 샘플링 데이터 구조체
	struct st_PROFILE_DATA
	{
		uint16_t                 s_ErrorFlag;
		std::wstring             s_Name;
		LARGE_INTEGER            s_StartTick;
		uint64_t                 s_TotalTime;
		uint64_t                 s_Min;
		uint64_t                 s_Max;
		uint64_t                 s_CallTime;
	};

	typedef std::unordered_map<std::wstring, st_PROFILE_DATA>* PROFILE_MAP_PTR;

	struct st_TABLE_PTR
	{
		PROFILE_MAP_PTR s_TablePtr;
		DWORD           s_ThreadID;
	};

	struct st_TOTAL_DATA
	{
		BOOL            s_UseFlag;
		std::wstring    s_Name;
		DOUBLE          s_TotalAvgTime;
		DOUBLE          s_TotalMaxTime;
		DOUBLE          s_TotalMinTime;
		uint64_t        s_TotalCallTime;
		INT             s_Cnt; //더해줄때 마다 증가시키기
	};


private:
	DWORD                        m_TlsIndex;
	uint16_t                     m_ArrayIndex;
	SRWLOCK                      m_ArrayLock;
	st_TABLE_PTR                 m_SampleArray[MAX_THREAD_COUNT];
						
	std::unordered_map<std::wstring, st_TOTAL_DATA> m_TotalDataTable;


private:
	CProfilerManager()
	{

	}
	~CProfilerManager()
	{

	}

public:
	/////////////////////////////////////////////////////////////////////////////
	// 프로파일러를 위한 TlsIndex 저장 및 동기화 객체 초기화
	// 
	// Parameters: 없음
	// Return: TlsAlloc 성공(true), 실패(false)
	/////////////////////////////////////////////////////////////////////////////
	bool                                Init()
	{
		//TlsIndex 저장
		m_TlsIndex = TlsAlloc();
		if (m_TlsIndex == TLS_OUT_OF_INDEXES)
		{
			wprintf(L"CProfilerManger_Init()_TlsAlloc Error : %d \n", GetLastError());
			return false;
		}

		//동기화 객체 초기화
		InitializeSRWLock(&m_ArrayLock);

		m_ArrayIndex = 0;

		return true;
	}


	/////////////////////////////////////////////////////////////////////////////
	// TLS에 객체 저장.
	// 스레드 시작시 호출해야 함.
	// Parameters: 없음
	// Return: 전체 샘플링 데이터 배열에 세팅 가능하면 성공 안되면 실패
	/////////////////////////////////////////////////////////////////////////////
	bool                                ProfileInit()
	{
		AcquireSRWLockExclusive(&m_ArrayLock);

		if (m_ArrayIndex >= MAX_THREAD_COUNT)
		{
			wprintf(L"ProfilerManager_ProfileInit()_Thread Count Max Error \n");
			ReleaseSRWLockExclusive(&m_ArrayLock);
			return false;
		}

		m_SampleArray[m_ArrayIndex].s_ThreadID = GetCurrentThreadId();
		m_SampleArray[m_ArrayIndex].s_TablePtr = new std::unordered_map<std::wstring, st_PROFILE_DATA>;

		if (!TlsSetValue(m_TlsIndex, m_SampleArray[m_ArrayIndex].s_TablePtr))
		{
			wprintf(L"ProfilerManager_ProfileInit()_TlsSetValue Error : %d \n", GetLastError());
			ReleaseSRWLockExclusive(&m_ArrayLock);
			return false;
		}

		m_ArrayIndex++;
		ReleaseSRWLockExclusive(&m_ArrayLock);

		return true;
	}


	/////////////////////////////////////////////////////////////////////////////
    // Profiling 된 데이타를 Text 파일로 출력한다.
    //
    // Parameters: (char *)출력될 파일 이름.
    // Return: 없음.
    /////////////////////////////////////////////////////////////////////////////
	bool                                ProfileDataOutText(WCHAR* szName)
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
		st_PROFILE_DATA* ptr[MAX_THREAD_COUNT];
		size_t           size;
		INT              j = 0;
		for (int i = 0; i < m_ArrayIndex; i++)
		{
			size = m_SampleArray[i].s_TablePtr->size();

			//해시 테이블 size 만큼 st_PROFILE_DATA 배열 생성해서 그 포인터를 ptr[i]에 저장
			ptr[i] = new st_PROFILE_DATA[size];

			std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = m_SampleArray[i].s_TablePtr->begin();
			j = 0;

			//한 스레드의 해시 테이블을 iterator로 순회하면서 저장된 value를 ptr[i][j]에 있는 st_PROFILE_DATA 원소에 Copy
			for (; it != m_SampleArray[i].s_TablePtr->end();)
			{
				ptr[i][j].s_CallTime = it->second.s_CallTime;
				ptr[i][j].s_TotalTime = it->second.s_TotalTime;
				ptr[i][j].s_Name = it->second.s_Name;

				ptr[i][j].s_Max = it->second.s_Max;
				ptr[i][j].s_Min = it->second.s_Min;

				j++;
				++it;
			}
		}

		//=======================================================================================================
		// 
		//각 스레드 별 샘플링 데이터 계산 및 출력, 취합 출력
		// 
		//=======================================================================================================

		const WCHAR* line = L"+------------+------------------------------------------+--------------+--------------+--------------+------------+\n";
		const WCHAR* header = L"| Thread ID  | Name                                     | Avg (us)     | Min (us)     | Max (us)     | Call     |\n";
		const WCHAR* row = L"| %10u | %-40s | %12.3f | %12.3f | %12.3f | %10lld |\n";
		const WCHAR* total = L"| %-10s | %-40s | %12.3f | %12.3f | %12.3f | %10lld |\n";

		//각 스레드 해시 테이블 size 합산
		size_t totalsize = 0;
		size_t maxsize = 0;
		for (int i = 0; i < m_ArrayIndex; i++)
		{
			maxsize = max(maxsize, m_SampleArray[i].s_TablePtr->size());
			totalsize += m_SampleArray[i].s_TablePtr->size();
		}


		//Total 합산을 위한 배열 생성 및 Tag 초기화 작업
		st_TOTAL_DATA* pTotal = new st_TOTAL_DATA[totalsize];
		for (int i = 0; i < totalsize; i++)
		{
			pTotal[i].s_UseFlag = 0;
			pTotal[i].s_Cnt = 0;
		}


		// 계산된 정보를 파일에 저장할 버퍼에 Copy(헤더도 이때 저장)
		// 프로파일러 헤더 및 데이터 저장
		// MAX_STRING_SIZE : 파일에 저장할 정보들 문자열 저장시 1줄 최대 크기
		size_t reMain = (MAX_HEADER_SIZE + (MAX_STRING_SIZE) * (totalsize + maxsize + m_ArrayIndex)) * sizeof(WCHAR);
		size_t bufferSize = reMain;
		WCHAR* pFileBuffer = (WCHAR*)malloc(reMain);
		WCHAR* pEnd = nullptr;
		HRESULT ret;

		memset(pFileBuffer, NULL, reMain);

		// 1. 헤더 저장
		// 3번째 인자 : 문자열 Copy후 문자열 끝 NULL 가리키는 포인터
		// 4번째 인자 : Dest 버퍼에 문자열 Copy후 남은 크기
		ret = StringCchPrintfExW(pFileBuffer, reMain, &pEnd, &reMain, 0, line);
		if (ret != S_OK)
		{
			for (int i = 0; i < m_ArrayIndex; i++)
				delete[] ptr[i];

			delete[] pTotal;

			free(pFileBuffer);

			return false;
		}

		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, header);
		if (ret != S_OK)
		{
			for (int i = 0; i < m_ArrayIndex; i++)
				delete[] ptr[i];

			delete[] pTotal;

			free(pFileBuffer);

			return false;
		}

		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, line);
		if (ret != S_OK)
		{
			for (int i = 0; i < m_ArrayIndex; i++)
				delete[] ptr[i];

			delete[] pTotal;

			free(pFileBuffer);

			return false;
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
		for (int i = 0; i < m_ArrayIndex; i++)
		{
			//해시 테이블 값 저장한 배열 순회
			tableSize = m_SampleArray[i].s_TablePtr->size();
			threadID = m_SampleArray[i].s_ThreadID;

			for (int j = 0; j < tableSize;j++)
			{
				//전체 TotalTick에서 Max, Min 뺀 값을 call 횟수로 나눔
				maxTime = (double)ptr[i][j].s_Max  * 1000000.0 / (double)freq.QuadPart;
				minTime = (double)ptr[i][j].s_Min  * 1000000.0 / (double)freq.QuadPart;

				if (ptr[i][j].s_CallTime > 2)
				{
					avgTime = (((ptr[i][j].s_TotalTime - (double)ptr[i][j].s_Max - ptr[i][j].s_Min) / (ptr[i][j].s_CallTime - 2)) * (double)1000000 / freq.QuadPart);
				}
				else
				{
					avgTime = ptr[i][j].s_TotalTime / ptr[i][j].s_CallTime * (double)1000000 / freq.QuadPart;
				}


				ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0,row, threadID, ptr[i][j].s_Name.c_str(), avgTime, minTime, maxTime, (long long)ptr[i][j].s_CallTime);
				if (ret != S_OK)
				{
					for (int i = 0; i < m_ArrayIndex; i++)
						delete[] ptr[i];

					delete[] pTotal;

					free(pFileBuffer);

					return false;
				}

				//Total배열 순회 하면서 같은 Tag있으면 더해주기
				int k;
				for (k = 0; k < totalsize; k++)
				{
					//같은 Tag를 가질때
					if (pTotal[k].s_Name == ptr[i][j].s_Name)
					{
						pTotal[k].s_TotalCallTime += ptr[i][j].s_CallTime;
						pTotal[k].s_TotalAvgTime += avgTime;
						pTotal[k].s_TotalMaxTime += maxTime;
						pTotal[k].s_TotalMinTime += minTime;
						pTotal[k].s_UseFlag = 1;
						pTotal[k].s_Cnt++;
						break;
					}

				}

				//만약 ptr[i][j]에 저장된 st_PROFILE_DATA의 Tag를 가지고 Total배열 반복물 다 돌아도 같은 Tag를 못찾았으면 k는 totalsize값일 것임.
				if (k == totalsize)
				{
					//다시 Total 배열 돌면서 사용안하는 공간 찾기
					for (int l = 0; l < totalsize; l++)
					{
						//사용 안하는 공간이라면 
						if (pTotal[l].s_UseFlag == 0)
						{
							pTotal[l].s_Name = ptr[i][j].s_Name;
							pTotal[l].s_TotalCallTime = ptr[i][j].s_CallTime;
							pTotal[l].s_TotalAvgTime = avgTime;
							pTotal[l].s_TotalMaxTime = maxTime;
							pTotal[l].s_TotalMinTime = minTime;
							pTotal[l].s_UseFlag = 1;
							pTotal[l].s_Cnt = 1;
							break;
						}
					}

				}

			}

			//한 스레드 샘플 데이터 Copy했으면 ----- 만들기
			ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, line);
			if (ret != S_OK)
			{
				for (int i = 0; i < m_ArrayIndex; i++)
					delete[] ptr[i];

				delete[] pTotal;

				free(pFileBuffer);

				return false;
			}
		}

		//Total 배열에 있는 값 출력하기
		INT cnt;
 		for (int i = 0; i < totalsize; i++)
		{
			//사용안하는 배열이면 Pass
			if (pTotal[i].s_UseFlag == 0)
				continue;

			cnt = pTotal[i].s_Cnt;

			ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, total, L"TOTAL", pTotal[i].s_Name.c_str(), pTotal[i].s_TotalAvgTime / cnt, pTotal[i].s_TotalMinTime / cnt, pTotal[i].s_TotalMaxTime / cnt, (long long)pTotal[i].s_TotalCallTime);
			if (ret != S_OK)
			{
				for (int i = 0; i < m_ArrayIndex; i++)
					delete[] ptr[i];

				delete[] pTotal;

				free(pFileBuffer);

				return false;
			}
		}



		// 3. 파일 스트림 생성
		err = _wfopen_s(&fp, filename, L"wb");
		if (err != 0)
		{
			for (int i = 0; i < m_ArrayIndex; i++)
				delete[] ptr[i];

			delete[] pTotal;

			free(pFileBuffer);

			return false;
		}


		//저장
		fwrite(pFileBuffer, bufferSize, 1, fp);


		// 4. 스트림 닫기 및 뒷 정리
		fclose(fp);

		for (int i = 0; i < m_ArrayIndex; i++)
			delete[] ptr[i];

		delete[] pTotal;

 		free(pFileBuffer);


		return true;
	}


	inline        uint16_t              GetTlsIndex()
	{
		return m_TlsIndex;
	}

	inline static CProfilerManager*     GetInstance()
	{
		static CProfilerManager Cpm;
		return &Cpm;
	}

};


class CProfiler 
{
private:
	// 프로파일러 샘플링 데이터 구조체
	struct st_PROFILE_DATA
	{
		uint16_t       s_ErrorFlag;
		std::wstring   s_Name;
		LARGE_INTEGER  s_StartTick;
		uint64_t       s_TotalTime;
		uint64_t       s_Min;
		uint64_t       s_Max;
		uint64_t       s_CallTime;
	};

	typedef std::unordered_map<std::wstring, st_PROFILE_DATA>* PROFILE_MAP_PTR;

private:
	// 멤버 변수
	PCWCHAR            m_Tag;               //생성자에서 전달된 Tag 문자열 포인터
	st_PROFILE_DATA    m_SampleData;        //Begin, End에서 사용할 샘플링 데이터 담을 변수

public:
	CProfiler(const WCHAR* tag)
	{
		PRO_BEGIN(const_cast<WCHAR*>(tag));
		m_Tag = tag;
	}

	~CProfiler()
	{
		PRO_END(const_cast<WCHAR*>(m_Tag));
	}

	/////////////////////////////////////////////////////////////////////////////
    // 하나의 함수 Profiling 시작, 끝 함수.
    //
    // Parameters: (char *)Profiling이름.
    // Return: 없음.                    
    /////////////////////////////////////////////////////////////////////////////
	void ProfileBegin(WCHAR* szName)
	{
		std::wstring str = szName;


		if (str.size() >= MAX_STRING_SIZE)
			return;

		//스레드 샘플링 데이터 자료구조에 접근
		PROFILE_MAP_PTR pTable;
		pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfilerManager::GetInstance()->GetTlsIndex());

		std::unordered_map<std::wstring,st_PROFILE_DATA>::iterator it = pTable->find(str);


		// wstring str을 key로 할때 해시 테이블에 해당 key가 없는 경우 초기값 세팅 후 테이블에 저장
		if (it == pTable->end())
		{
			m_SampleData.s_CallTime = 0;
			m_SampleData.s_TotalTime = 0;
			m_SampleData.s_ErrorFlag = 0;
			m_SampleData.s_Name = str;

			for (int i = 0; i < 2; i++)
			{
				m_SampleData.s_Max = 0;
				m_SampleData.s_Min = ULLONG_MAX;
			}

		}

		else
		{
			//해시 테이블에 있으면 그 값으로 SampleData 세팅
			m_SampleData.s_CallTime = it->second.s_CallTime;
			m_SampleData.s_ErrorFlag = it->second.s_ErrorFlag;
			m_SampleData.s_TotalTime = it->second.s_TotalTime;
			m_SampleData.s_Name = it->second.s_Name;

			m_SampleData.s_Max = it->second.s_Max;
			m_SampleData.s_Min = it->second.s_Min;
		}


		//ErrorFlag가 0이거나 End에서 설정한 플래그 값인 0x0010이 아니면 Error를 발생시킴
		if (m_SampleData.s_ErrorFlag != 0 && m_SampleData.s_ErrorFlag != 0x0010)
			throw szName;


		//정상적으로 세팅 되었으면 Begin 제대로 했으니 ErrorFlag 세팅
		m_SampleData.s_ErrorFlag = 0xFFFF;

		//시간 측정 시작
		QueryPerformanceCounter(&m_SampleData.s_StartTick);
	}

	void ProfileEnd(WCHAR* szName)
	{
		//종료 시간 갱신
		LARGE_INTEGER endTick;
		uint64_t DiffTick;

		QueryPerformanceCounter(&endTick);

		//생성자에서 설정한 멤버 변수 에러 플래그랑 다르면 에러
		if (m_SampleData.s_ErrorFlag != 0xFFFF)
			throw szName;

		//Total에 합산
		DiffTick = (endTick.QuadPart - m_SampleData.s_StartTick.QuadPart);
		m_SampleData.s_TotalTime += DiffTick;

		//MAX값 갱신
		if (m_SampleData.s_Max < DiffTick)
		{
			m_SampleData.s_Max = DiffTick;
		}

		//MIN값 갱신
		if (m_SampleData.s_Min > DiffTick)
		{
			m_SampleData.s_Min = DiffTick;
		}

		//호출량 증가
		m_SampleData.s_CallTime++;

		//에러 플래그 설정
		m_SampleData.s_ErrorFlag = 0x0010;


		//멤버 변수에 저장된 값 갱신 후 해시 테이블에 저장
		PROFILE_MAP_PTR pTable;
		pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfilerManager::GetInstance()->GetTlsIndex());
		
		std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = pTable->find(m_SampleData.s_Name);

		//새로운 Tag일때
		if (it == pTable->end())
		{
			pTable->insert(std::pair<std::wstring, st_PROFILE_DATA>(m_SampleData.s_Name, m_SampleData));
			return;
		}

		//이미 있으면
		it->second = m_SampleData;
		return;
	}
};

