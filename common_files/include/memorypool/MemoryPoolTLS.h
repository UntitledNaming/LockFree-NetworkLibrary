#pragma once
#include <windows.h>
#include <iostream>
#include <unordered_map>
#include "LockFreeMemoryPoolLive.h"
#include "LFStack.h"

#define SUBPOOL_MAX 100
#define BUCKET_SIZE 1000
#define BIT_MASK    0x00007FFFFFFFFFFF

template<typename U>
class CMPoolTLS 
{
protected:

	class CSubPool;

	struct Node
	{
		U         s_data;
		Node*     s_pNext;
		UINT64    s_poolid;
	};

	struct Bucket
	{
		Node* s_Top;
		UINT  s_Cnt;                                //버킷이 들고 있는 노드 갯수
	};

	struct SubPool
	{
		DWORD     s_ThreadId;
		CSubPool* s_ptr;
	};

public:
	LFStack<Bucket*>*    g_pBucketPool;                // 공용 버킷 풀 락프리 스택으로 구현
	CMemoryPool<Bucket>* g_pCaseBucketPool;            // 버킷 껍데기 가져오는 풀

protected:
	BOOL          m_bPlacementNew;
	UINT64        m_iOriginID;
	UINT          m_iBKTCapacity;
	UINT64        m_iUseCnt;

	DWORD         m_iTlsIndex;

	LONG          m_SubPoolIndex;
	SubPool       m_SubPoolArray[SUBPOOL_MAX];                //각 서브풀 포인터 관리 자료구조

public:
	CMPoolTLS(int iBucketNum = 0, bool bPlacementNew = false) 
	{
		Bucket* pBucket = nullptr;

		m_iTlsIndex = TlsAlloc();

		if (m_iTlsIndex == TLS_OUT_OF_INDEXES)
		{
			wprintf(L"CMPoolTLS()_TlsAlloc Error : %d \n", GetLastError());
			return;
		}

		//멤버 변수 초기화
		m_bPlacementNew = bPlacementNew;
		m_iBKTCapacity = iBucketNum;
		m_SubPoolIndex = -1;
		m_iUseCnt = 0;


		//메모리 풀 ID 설정. (멤버의 주소값은 고유함)
		m_iOriginID = (UINT64)&m_iUseCnt;

		//공용 버킷 락프리 스택 초기화
		g_pBucketPool = new LFStack<Bucket*>;
		g_pBucketPool->Clear();

		g_pCaseBucketPool = new CMemoryPool<Bucket>;

		for (int i = 0; i < iBucketNum; i++)
		{
			pBucket = BucketAlloc();
			g_pBucketPool->Push(pBucket);
		}

	}
	~CMPoolTLS()
	{
		// 서브 풀의 노드 반납
		for (int i = 0; i < m_SubPoolIndex; i++)
		{
			if (m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top != nullptr)
			{
				// 할당 버킷에 있는 노드 반납
				while (1)
				{
					Node* next = m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top->s_pNext;
					
					if(m_bPlacementNew == false)
						m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top->s_data.~U();

					free(m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top);
					m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top = next;

					if (m_SubPoolArray[i].s_ptr->m_iAlloc->s_Top == nullptr)
						break;
				}

			}

			if (m_SubPoolArray[i].s_ptr->m_iRet->s_Top != nullptr)
			{
				// 반환 버킷에 있는 노드 반납
				while (1)
				{
					Node* next = m_SubPoolArray[i].s_ptr->m_iRet->s_Top->s_pNext;

					if (m_bPlacementNew == false)
						m_SubPoolArray[i].s_ptr->m_iRet->s_Top->s_data.~U();

					free(m_SubPoolArray[i].s_ptr->m_iRet->s_Top);
					m_SubPoolArray[i].s_ptr->m_iRet->s_Top = next;

					if (m_SubPoolArray[i].s_ptr->m_iRet->s_Top == nullptr)
						break;
				}

			}

			// 할당 버킷 껍데기 반납
			g_pCaseBucketPool->Free(m_SubPoolArray[i].s_ptr->m_iAlloc);

			// 반환 버킷 껍데기 반납
			g_pCaseBucketPool->Free(m_SubPoolArray[i].s_ptr->m_iRet);
		}

		// 메인 풀의 노드 반납
		while (1)
		{
			// 메인 풀에서 버킷 하나 뽑기
			Bucket* pBucket = nullptr;
			if (!g_pBucketPool->Pop(pBucket))
				break;

			// 버킷 하나에서 노드 뽑아서 반납
			while (1)
			{
				Node* next = pBucket->s_Top->s_pNext;

				if (m_bPlacementNew == false)
					pBucket->s_Top->s_data.~U();

				free(pBucket->s_Top);
				pBucket->s_Top = next;

				if (pBucket->s_Top == nullptr)
					break;
			}

			// 버킷 껍데기 반환
			g_pCaseBucketPool->Free(pBucket);
		}


		delete g_pBucketPool;
		delete g_pCaseBucketPool;
		g_pBucketPool = nullptr;
		g_pCaseBucketPool = nullptr;
	}

	Bucket* BucketAlloc()
	{

		Bucket* pBucket = g_pCaseBucketPool->Alloc();
		pBucket->s_Top = nullptr;
		pBucket->s_Cnt = BUCKET_SIZE;

		//버킷에 노드 할당
		for (int i = 0; i < BUCKET_SIZE; i++)
		{
			//노드 생성 및 초기화
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->s_poolid = m_iOriginID;
			newNode->s_pNext = nullptr;

			//객체 생성자 호출
			new(&newNode->s_data) U;

			newNode->s_pNext = pBucket->s_Top;
			pBucket->s_Top = newNode;

		}

		InterlockedIncrement(&m_iBKTCapacity);

		return pBucket;
	}

	//////////////////////////////////////////////////////////////////////////
    // 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
    //
    // Parameters: 없음.
    // Return: (int) 메모리 풀 내부 전체 개수
    //////////////////////////////////////////////////////////////////////////
	inline int GetCapacityCnt()
	{
		return m_iBKTCapacity;
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//  각 서브 풀의 UseCnt 합산.
	//
	//////////////////////////////////////////////////////////////////////////
	inline int GetUseCnt()
	{
		return m_iUseCnt;
	}


	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.  
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	U* Alloc()
	{
		CSubPool* ret;
		UINT      retindex;
		ret = (CSubPool*)TlsGetValue(m_iTlsIndex);
		if (ret == nullptr)
		{
			ret = new CSubPool(this);
			TlsSetValue(m_iTlsIndex, ret);
			retindex = InterlockedIncrement(&m_SubPoolIndex);
			if (retindex >= SUBPOOL_MAX)
				__debugbreak();

			m_SubPoolArray[retindex].s_ptr = ret;
			m_SubPoolArray[retindex].s_ThreadId = GetCurrentThreadId();
		}


		InterlockedIncrement64((__int64*) & m_iUseCnt);
		return ret->Alloc();
	}


	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool Free(U* pData)
	{
		CSubPool* ret;
		UINT      retindex;

		ret = (CSubPool*)TlsGetValue(m_iTlsIndex);
		if (ret == nullptr)
		{
			//다른 스레드에서 Alloc한게 내 스레드에서 먼저 Free 호출 될 수 있음.
			ret = new CSubPool(this);
			TlsSetValue(m_iTlsIndex, ret);

			retindex = InterlockedIncrement(&m_SubPoolIndex);
			if (retindex >= SUBPOOL_MAX)
				__debugbreak();

			m_SubPoolArray[retindex].s_ptr = ret;
			m_SubPoolArray[retindex].s_ThreadId = GetCurrentThreadId();

		}

		InterlockedDecrement64((__int64*) & m_iUseCnt);

		return (ret->Free(pData));
	}
	
	class CSubPool
	{
		using Bucket = CMPoolTLS<U>::Bucket;
		using Node = CMPoolTLS<U>::Node;

	public:
		Bucket*    m_iAlloc;    //할당 버킷 포인터 변수
		Bucket*    m_iRet;      //반환용 버킷 포인터 변수
		CMPoolTLS* m_parent;
		INT        m_AllocCnt;
		INT        m_FreeCnt;

	public:
		CSubPool(CMPoolTLS* parent) : m_parent(parent)
		{
			//버킷 할당 하기
			if (!m_parent->g_pBucketPool->Pop(m_iAlloc))
			{
				// 공용 버킷 락프리 스택에 버킷* 원소가 없는 경우
				// 버킷을 바로 생성해서 설정해버리기
				m_iAlloc = m_parent->BucketAlloc();
			}

			//SubPool은 MemoryPoolTLS 생성자 호출 후에 생성되기 때문에 순서 걱정 안해도 됨.
			m_iRet = m_parent->g_pCaseBucketPool->Alloc();
			m_iRet->s_Cnt = 0;
			m_iRet->s_Top = nullptr;
			m_AllocCnt = 0;
			m_FreeCnt = 0;
		}
		~CSubPool()
		{

		}

		U* Alloc()
		{
			Node* temp;
			INT AllocCnt;
			INT FreeCnt;


			if (m_iAlloc->s_Top == nullptr)
			{
				AllocCnt = m_AllocCnt;
				FreeCnt = m_FreeCnt;

				m_parent->g_pCaseBucketPool->Free(m_iAlloc);

				//버킷에 있는 노드 다 꺼낸 경우 공용 버킷 락프리 스택에서 Pop해서 가져오기
				if (!m_parent->g_pBucketPool->Pop(m_iAlloc))
				{
					m_iAlloc = m_parent->BucketAlloc();
				}
			}

			m_iAlloc->s_Cnt--;

			//현재 버킷의 top 임시 저장
			temp = m_iAlloc->s_Top;

			//top 변경
			m_iAlloc->s_Top = temp->s_pNext;

			m_AllocCnt++;

			//placement == true면 생성자 호출
			if(m_parent->m_bPlacementNew == true)
				new(&temp->s_data) U;

			return &(temp->s_data);
		}

		bool Free(U* pData)
		{
			//반환받은 객체 주소를 통해 실제 메모리 풀 노드 주소 구하기
			Node* newNode = (Node*)pData;

			//다른 메모리풀의 노드를 반환했을 때 그냥 false return하기
			if (newNode->s_poolid != m_parent->m_iOriginID)
				return false;

			//bPlacementNew 체크해서 true 면 소멸자 호출해주기
			if (m_parent->m_bPlacementNew == true)
				pData->~U();
			

			//기존 버킷에 넣고 제한량 넘었으면 반환용 버킷에 넣기

			//기존 버킷 사용량 체크
			if (m_iAlloc->s_Cnt == BUCKET_SIZE)
			{
				//버킷에 갯수 다 찼으면 반환 버킷에 넣기

				//반환 버킷에 넣을 때 만약에 버킷 다 찼으면 락프리 스택에 버킷 포인터 넣어주기
				if (m_iRet->s_Cnt == BUCKET_SIZE)
				{
					m_parent->g_pBucketPool->Push(m_iRet);

					//새로운 버킷 껍데기 가져오기
					m_iRet = m_parent->g_pCaseBucketPool->Alloc();
					m_iRet->s_Cnt = 0;
					m_iRet->s_Top = nullptr;
				}

				//기존 반환 버킷에 노드 넣고 끝
				m_iRet->s_Cnt++;
				newNode->s_pNext = m_iRet->s_Top;
				m_iRet->s_Top = newNode;


				return true;
			}



			//기존 버킷 안 찼으면 head, cnt 갱신
			newNode->s_pNext = m_iAlloc->s_Top;

			m_iAlloc->s_Top = newNode;
			m_iAlloc->s_Cnt++;

			m_FreeCnt++;
			return true;
		}

	};

};


