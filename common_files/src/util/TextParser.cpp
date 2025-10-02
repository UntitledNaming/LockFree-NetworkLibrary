#include <iostream>
#include <windows.h>
#include "TextParser.h"
#pragma warning(disable:4996)
// 인자로 전달된 파일이름을 개방하고 메모리 동적할당해서 파일의 데이터를 전부 메모리에 옮기는 기능

Parser::Parser()
{
}

Parser::~Parser()
{
	free(_pTextBuffer);
}

bool Parser::LoadFile(const char* FileName)
{
	strcpy(_FileName, FileName);

	int FILESIZE = 0;
	FILE* fp;
	errno_t err;

	err = fopen_s(&fp, _FileName, "rt");
	if (err != 0)
	{
		std::cout << "파일 읽기 실패" << std::endl;
		return false;
	}



	fseek(fp, 0, SEEK_END);
	FILESIZE = ftell(fp);
	_pTextBuffer = (char*)malloc(FILESIZE);
	_pRead = _pTextBuffer;
	fseek(fp, 0, SEEK_SET);
	fread(_pTextBuffer, FILESIZE, 1, fp);

	fclose(fp);

	return true;
}

//pRead를 읽을 필요 없는 문자열 저장된 주소 건너 뛰고 읽어도 되는 주소로 옮기기
//읽을 필요 없는 문자열 : 공백, 주석, 개행
bool Parser::SkipNoneWord()
{
	while (1)
	{
		//버퍼에 공백과 개행이 저장된 주소는 건너 뛰기
		if (*_pRead == ' ' || *_pRead == 0x0a || *_pRead == 0x09 || *_pRead == ';')
		{
			_pRead++;
			continue;
		}

		// /문자를 만나면 다음 문자가 /혹은 *이면 주석으로 처리, 주석이 아니면 반복문 탈출
		else if (*_pRead == '/')
		{
			char* temp = _pRead;
			temp++;

			//  //문법의 주석이니 pRead를 옮겨야 함. //는 개행이 나오면 그 다음 주소로 옮겨주면 됨. 옮기고 다시 반복문 처음으로 돌아가서 공백, 개행부터 검사
			if (*temp == '/')
			{
				_pRead = temp;
				while (1)
				{
					_pRead++;
					if (*_pRead == 0x0a)
					{
						_pRead++;
						break;
					}
				}
				continue;
			}

			// /**/문법의 주석이니 */끝나고 / 다음 주소로 _pRead 옮기고 다시 반복문 처음으로 돌아가서 공백, 개행부터 검사
			else if (*temp == '*')
			{
				_pRead = temp;
				while (1)
				{
					_pRead++;
					if (*_pRead == '*')
					{
						temp = _pRead;
						temp++;
						if (*temp == '/')
						{
							_pRead = temp;
							_pRead++;
							break;
						}

					}
				}
				continue;
			}

		}

		//공백, 개행, 주석이 아니라면 _pRead 그대로 두고 함수 탈출
		else
			return true;


	}
	return false;
}

//버퍼에 저장된 문자열 중에 단어로 판정된 문자의 주소를 알려주기 위해 주소값을 저장하는 char*의 주소값을 전달.
//그 단어의 길이가 몇인지 
bool Parser::GetNextWord(char** chppBuffer, int* lpLength)
{
	int count = 0;
	bool Start = false;

	SkipNoneWord();

	while (1)
	{
		if (_pRead == nullptr)
			return false;

		//단어의 시작 위치에서 읽는 포인터를 계속 이동 시키는데 아래와 같은 문자를 만나면 단어가 끝난 것으로 인식
		if (*_pRead == ',' || *_pRead == 0x20 || *_pRead == 0x08 || *_pRead == 0x09 || *_pRead == 0x0a
			|| *_pRead == 0x0d || *_pRead == ';')
		{
			break;
		}
		
		else if (*_pRead == '"')
		{
			_pRead++;
			count++;
			break;
		}

		_pRead++;
		count++;
	}

	*chppBuffer = _pRead - count;

	*lpLength = count;

	return true;
}

//객체를 전달하고 싶으면 클래스에서 복사 대입 연산자 만들어 주기
bool Parser::GetValue(const char* szName, int* pValue)
{
	_pRead = _pTextBuffer; // Value를 얻을 때 마다 전체 메모리 버퍼를 처음부터 탐색
	char* chpBuff;
	char chWord[256];
	int iLength;

	//어떤 단어가 발견될 때 while문 안으로 진입
	while (Parser::GetNextWord(&chpBuff, &iLength))
	{
		//chWord 배열 0으로 초기화
		memset(chWord, 0, 256);

		//chWord 배열에 chpBuff 포인터가 가리키는 문자부터 iLength 만큼 옮김. 
		memcpy(chWord, chpBuff, iLength);

		//찾고자 하는 문자열과 발견한 단어가 동일하다면
		if (strcmp(szName, chWord) == 0)
		{
			// 어떤 단어를 발견할 때까지_pRead 이동
			if (Parser::GetNextWord(&chpBuff, &iLength))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLength);
				//발견한 문자가 =일때
				if (strcmp(chWord, "=") == 0)
				{
					if (GetNextWord(&chpBuff, &iLength))
					{

						//문자열 아닌 경우
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLength);
						*pValue = atoi(chWord);

						return true;
					}
					return false;
				}
			}
			return false;
		}
	}
	return false;
}

// 반환 문자열 NULL 포함 및 Len도 NULL 포함
bool Parser::GetValue(const char* szName, st_Msg* pMsg)
{
	_pRead = _pTextBuffer; // Value를 얻을 때 마다 전체 메모리 버퍼를 처음부터 탐색
	char* chpBuff;
	char chWord[256];
	int iLength;

	//어떤 단어가 발견될 때 while문 안으로 진입
	while (Parser::GetNextWord(&chpBuff, &iLength))
	{
		//chWord 배열 0으로 초기화
		memset(chWord, 0, 256);
		//chWord 배열에 chpBuff 포인터가 가리키는 문자부터 iLength 만큼 옮김. 
		memcpy(chWord, chpBuff, iLength);

		//찾고자 하는 문자열과 발견한 단어가 동일하다면
		if (strcmp(szName, chWord) == 0)
		{
			// 어떤 단어를 발견할 때까지_pRead 이동
			if (Parser::GetNextWord(&chpBuff, &iLength))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLength);
				//발견한 문자가 =일때
				if (strcmp(chWord, "=") == 0)
				{
					if (Parser::GetNextWord(&chpBuff, &iLength))
					{
						// " 문자 발견할 때 까지 이동
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLength);

						if (strcmp(chWord, "\"") == 0)
						{
							if (GetNextWord(&chpBuff, &iLength))
							{
								//찾은 문자열의 주소를 아웃파라미터에 담아서 외부에 넘김.
								memcpy(pMsg->s_ptr, chpBuff, iLength-1); // 맨뒤 " 붙은 것 제거
								*(pMsg->s_ptr + iLength - 1) = NULL;
								pMsg->s_len = iLength;
								return true;
							}

						}

					}
				}
			}
		}
	}
	return false;
}


