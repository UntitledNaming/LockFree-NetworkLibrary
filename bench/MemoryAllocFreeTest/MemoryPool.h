#pragma once
#include <new.h>
#include <malloc.h>
#include <windows.h>

typedef bool     MYBOOL;

template <typename T>
class MemoryPool
{
private:

	//////////////////////////////////////////////////////////////////////////
	// 메모리 풀에서 사용할 노드 구조체
	// s_data   : 노드에 객체 자체를 저장
	// s_pNext  : 노드의 다음 주소
	// s_poolID : 타입별 메모리 풀 마다 ID 부여할때 체크하기 위한 변수
	//////////////////////////////////////////////////////////////////////////
	struct Node
	{
		T        s_data;
		Node*    s_pNext;
		UINT64   s_poolD;
	};

private:

private:
	Node* m_pTopNode;
	MYBOOL                              m_bPlacementNew;
	UINT                                m_iCapacity;
	UINT                                m_iUseCnt;
	UINT64                              m_iOriginID;
	SRWLOCK                             m_lock;

public:
	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Parameters:	(int) 초기 블럭 개수.
	//				(bool) Alloc 시 생성자 / Free 시 파괴자 호출 여부
	// Return:
	//////////////////////////////////////////////////////////////////////////

	MemoryPool(int iBlockNum = 0, bool bPlacementNew = false) : m_bPlacementNew(bPlacementNew)
	{
		//메모리 풀 ID 설정. 
		m_iOriginID = (UINT64)&m_pTopNode;

		//멤버 변수 초기화
		m_pTopNode = nullptr;
		m_iUseCnt = 0;
		m_iCapacity = 0;
		InitializeSRWLock(&m_lock);

		for (int i = 0; i < iBlockNum; i++)
		{
			PushBack();
		}

	}

	~MemoryPool()
	{
		// Free에서 소멸자 호출 안했으니 소멸자 호출해주고 메모리 풀 노드 지우기

		Node* newTop;

		while (m_pTopNode != nullptr)
		{

			newTop = m_pTopNode->s_pNext;

			if (m_bPlacementNew == false)
			{
				//소멸자 호출
				(m_pTopNode->s_data).~T();
			}

			free(m_pTopNode);

			m_pTopNode = newTop;
		}
	}

	void PushBack()
	{
		//노드 생성 및 초기화
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//객체 생성자 호출
		if (m_bPlacementNew == false)
			new(&newNode->s_data) T;

		//기존 TopNode에 연결
		newNode->s_pNext = m_pTopNode;

		m_pTopNode = newNode;

		m_iCapacity++;
	}

	//Alloc에서 아웃파라미터로 노드 포인터 줄 것임
	void CPushBack(Node** ppNode)
	{
		//노드 생성 및 초기화
		Node* newNode = (Node*)malloc(sizeof(Node));
		newNode->s_pNext = nullptr;
		newNode->s_poolD = m_iOriginID;

		//객체 생성자 호출
		if (m_bPlacementNew == false)
			new(&newNode->s_data) T;

		m_iCapacity++;
		m_iUseCnt++;


		if (newNode == nullptr)
			__debugbreak();

		//아웃 파라미터로 준 변수에 동적할당한 노드 주기
		*ppNode = newNode;

	}


	//////////////////////////////////////////////////////////////////////////
	// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
	//
	// Parameters: 없음.
	// Return: (int) 메모리 풀 내부 전체 개수
	//////////////////////////////////////////////////////////////////////////
	inline int GetCapacityCnt()
	{
		return m_iCapacity;
	}

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 블럭 개수를 얻는다.
	//
	// Parameters: 없음.
	// Return: (int) 사용중인 블럭 개수.
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
	T* Alloc()
	{
		Node* real = nullptr;

		AcquireSRWLockExclusive(&m_lock);

		if (m_iUseCnt == m_iCapacity)
		{
			CPushBack(&real);

			ReleaseSRWLockExclusive(&m_lock);
			return &real->s_data;
		}

		real = m_pTopNode;
		m_pTopNode = m_pTopNode->s_pNext;

		//기존 Top노드 메모리 풀과 분리했으니 추가 작업하던지 바로 반환
		if (m_bPlacementNew == true)
		{
			new(&(real->s_data)) T;
		}

		if (&real->s_data == nullptr)
			__debugbreak();

		m_iUseCnt++;
		ReleaseSRWLockExclusive(&m_lock);
		return &(real->s_data);
	}


	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool Free(T* pData)
	{
		AcquireSRWLockExclusive(&m_lock);

		//반환받은 객체 주소를 통해 실제 메모리 풀 노드 주소 구하기
		Node* newNode = (Node*)pData;

		//다른 메모리풀의 노드를 반환했을 때 그냥 false return하기
		if (newNode->s_poolD != m_iOriginID)
		{
			ReleaseSRWLockExclusive(&m_lock);
			return false;
		}

		//bPlacementNew 체크해서 true 면 소멸자 호출해주기
		if (m_bPlacementNew == true)
			pData->~T();

		newNode->s_pNext = m_pTopNode;
		m_pTopNode = newNode;

		m_iUseCnt--;
		ReleaseSRWLockExclusive(&m_lock);
		return true;
	}



};
