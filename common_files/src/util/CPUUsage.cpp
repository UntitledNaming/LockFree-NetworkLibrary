#include <windows.h>
#include <stdio.h>
#include "CPUUsage.h"



CCpuUsage::CCpuUsage(HANDLE hProcess)
{
	//---------------------------------------------------------------------
	// ���μ��� �ڵ� �Է� ������ �ڱ� �ڽ��� ���
	//---------------------------------------------------------------------
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		m_hProcess = GetCurrentProcess();
	}

	//---------------------------------------------------------------------
	// ���μ��� ������ Ȯ��
	// ���μ��� ����� ���� CPU ������ �����⸦ �Ͽ� ���� ������ ����
	//---------------------------------------------------------------------
	SYSTEM_INFO SystemInfo;

	GetSystemInfo(&SystemInfo);
	m_iNumberOfProcessor = SystemInfo.dwNumberOfProcessors;


    m_fProcessorTotal = 0;
    m_fProcessorUser = 0;
    m_fProcessorKernel = 0;
   
    m_fProcessTotal = 0;
    m_fProcessUser = 0;
    m_fProcessKernel = 0;

	m_fProcessor_LastKernel.QuadPart = 0;
	m_fProcessor_LastUser.QuadPart = 0;
	m_fProcessor_LastIdle.QuadPart = 0;


	m_fProcess_LastKernel.QuadPart = 0;
	m_fProcess_LastUser.QuadPart = 0;
	m_fProcess_LastTime.QuadPart = 0;

	UpdateCpuTime();
}


//----------------------------------------------------------
// CPU ������ ������. 500ms ~ 1000ms ������ ȣ���� ����
//----------------------------------------------------------
void CCpuUsage::UpdateCpuTime()
{
	//------------------------------------------------------------------------------
	// ���μ��� ������ ������.
	// 
	// ��� ����ü�� FILETIME ������, ULARGE_INTEGER�� ������ ���Ƽ� �̸� �����
	// FILETIME ����ü�� 100ns ������ �ð� ������ ǥ���ϴ� ����ü��.
	//-------------------------------------------------------------------------------

	_ULARGE_INTEGER Idle;
	_ULARGE_INTEGER Kernel;
	_ULARGE_INTEGER User;

	//------------------------------------------------------------------------------
	// �ý��� ��� �ð��� ����
	// 
	// Idle �ð�  / Ŀ�� ��� �ð�(Idle ����) / ���� ��� Ÿ�� 
	//------------------------------------------------------------------------------
	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
	{
		return;
	}

	// Ŀ�� �ð����� Idle ���ԵǾ� ����. �׷��� ���� ��.
	ULONGLONG KernelDiff = Kernel.QuadPart - m_fProcessor_LastKernel.QuadPart;
	ULONGLONG UserDiff = User.QuadPart - m_fProcessor_LastUser.QuadPart;
	ULONGLONG IdleDiff = Idle.QuadPart - m_fProcessor_LastIdle.QuadPart;

	ULONGLONG Total = KernelDiff + UserDiff;
	ULONGLONG TimeDiff;

	if (Total == 0)
	{
		m_fProcessorUser = 0.0f;
		m_fProcessorKernel = 0.0f;
		m_fProcessorTotal = 0.0f;
	}
	else
	{
		//Ŀ�� Ÿ�ӿ� Idle Ÿ���� ������ ���� ����ϱ�
		m_fProcessorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		m_fProcessorUser = (float)((double)UserDiff / Total * 100.0f);
		m_fProcessorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);

	}

	//����
	m_fProcessor_LastKernel = Kernel;
	m_fProcessor_LastUser = User;
	m_fProcessor_LastIdle = Idle;


	//--------------------------------------------------------------------------
	// ������ ���μ��� ������ ������
	//--------------------------------------------------------------------------
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;


	//--------------------------------------------------------------------------
	// ���� 100ns ������ �ð��� ����. UTC �ð�
	// 
	// ���μ��� ���� �Ǵ� ����
	// 
	// a = ���� ������ �ý��� �ð�(������ ������ �ð�)
	// b = ���μ����� CPU ��� �ð�
	// 
	// a : 100 = b : ����
	// 
	// �� �������� b�� ���μ����� CPU ��� �ð��� ����.
	// 
	//--------------------------------------------------------------------------

	//--------------------------------------------------------------------------
	// ���� �ð��� �������� 100ns �ð��� ����(���� �ð� ���ϴ� �Լ�)
	//--------------------------------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime); 

	
	//--------------------------------------------------------------------------
	// �ش� ���μ����� ����� �ð��� ����.
	// 
	// �ι�°, ����°�� ����, ���� �ð����� �̻����.
	//--------------------------------------------------------------------------
	GetProcessTimes(m_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);

	//----------------------------------------------------------------------------------
	// ������ ����� ���μ��� �ð����� ���� ���ؼ� ������ ���� �ð��� �������� Ȯ��
	//----------------------------------------------------------------------------------
	TimeDiff = NowTime.QuadPart - m_fProcess_LastTime.QuadPart;
	UserDiff = User.QuadPart - m_fProcess_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - m_fProcess_LastKernel.QuadPart;

	Total = KernelDiff + UserDiff;


	m_fProcessTotal = (float)(Total / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);
	m_fProcessKernel = (float)(KernelDiff / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);
	m_fProcessUser = (float)(UserDiff / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);

	m_fProcess_LastTime = NowTime;
	m_fProcess_LastKernel = Kernel;
	m_fProcess_LastUser = User;
}
