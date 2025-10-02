#pragma once
#define MAX_NAME_SIZE 256
#define MAX_STINRG_SIZE 256


class Parser
{
public:
	struct st_Msg
	{
		char  s_ptr[MAX_STINRG_SIZE];
		int   s_len;
	};

public:
	Parser();
	~Parser();
private:
	bool SkipNoneWord();
	bool GetNextWord(char** chppBuffer, int* lpLength);
public:
	bool LoadFile(const char* FileName);
	bool GetValue(const char* szName, int* pValue);
	bool GetValue(const char* szName, st_Msg* pMsg);

private:
	char _FileName[MAX_NAME_SIZE] = { 0, };
	char* _pTextBuffer = nullptr;
	char* _pRead = nullptr;//버퍼를 읽기 위한 용도의 포인터
	char* p = nullptr;
};

