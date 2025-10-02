#pragma once
class CCpuUsage
{
public:
	//------------------------------------------------------------
	// 생성자, 확인 대상 프로세스 핸들, 미입력시 자기 자신
	//------------------------------------------------------------

	CCpuUsage(HANDLE hProcess = INVALID_HANDLE_VALUE);

	void  UpdateCpuTime();

	inline float ProcessorTotal() { return m_fProcessorTotal; }
	inline float ProcessorUser() { return m_fProcessorUser; }
	inline float ProcessorKernel() { return m_fProcessorKernel; }

	inline float ProcessTotal() { return m_fProcessTotal; }
	inline float ProcessUser() { return m_fProcessUser; }
	inline float ProcessKernel() { return m_fProcessKernel; }


private:

	HANDLE m_hProcess;
	int    m_iNumberOfProcessor;

	float  m_fProcessorTotal;
	float  m_fProcessorUser;
	float  m_fProcessorKernel;

	float  m_fProcessTotal;
	float  m_fProcessUser;
	float  m_fProcessKernel;

	//마지막 측정 시간
	ULARGE_INTEGER m_fProcessor_LastKernel;
	ULARGE_INTEGER m_fProcessor_LastUser;
	ULARGE_INTEGER m_fProcessor_LastIdle;


	ULARGE_INTEGER m_fProcess_LastKernel;
	ULARGE_INTEGER m_fProcess_LastUser;
	ULARGE_INTEGER m_fProcess_LastTime;


};