#include <tchar.h>
#include <windows.h>
#include <malloc.h>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <clocale>

#ifdef _UNICODE
#ifndef UNICODE
#define UNICODE
#endif
#endif

#ifdef UNICODE
#ifndef _UNICODE
#define _UNICODE
#endif
#endif

typedef std::basic_string<TCHAR> tstring;
typedef std::basic_ostream<TCHAR> tostream;
typedef std::basic_istream<TCHAR> tistream;
typedef std::basic_ostringstream<TCHAR> tostringstream;
typedef std::basic_istringstream<TCHAR> tistringstream;

#ifdef UNICODE
#define tcin std::wcin
#define tcout std::wcout
#else
#define tcin std::cin
#define tcout std::cout
#endif

const int STANDARD_PROCESS_IN_LIST=20;
const int ADDITIONAL_PROCESS_IN_LIST=5;

TCHAR *g_startPrefix=_T("ПУСК");
TCHAR *g_listPrefix=_T("СПИСОК");
TCHAR *g_stopPrefix=_T("КОНЕЦ");
TCHAR *g_exitPrefix=_T("ВЫХОД");

SECURITY_ATTRIBUTES g_mutexSA;
HANDLE g_mutex;

SECURITY_ATTRIBUTES g_jobSA;
HANDLE g_job;

bool isExit=false;

bool initMutexAndJob()
{
	// создание (получение) именованного мьютекса
	g_mutexSA.lpSecurityDescriptor = (PSECURITY_DESCRIPTOR)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(g_mutexSA.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
	// ACL is set as NULL in order to allow all access to the object.
	SetSecurityDescriptorDacl(g_mutexSA.lpSecurityDescriptor, TRUE, NULL, FALSE);
	g_mutexSA.nLength = sizeof(g_mutexSA);
	g_mutexSA.bInheritHandle = FALSE;

	g_mutex=CreateMutex(&g_mutexSA, FALSE, _T("Global\\courseTaskManagerMutex"));
	//_tcprintf(_T("Mutex exists: %d\n"), GetLastError()==ERROR_ALREADY_EXISTS);

	// создание (получение) именнованного списка процессов
	g_jobSA.lpSecurityDescriptor = (PSECURITY_DESCRIPTOR)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(g_jobSA.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
	// ACL is set as NULL in order to allow all access to the object.
	SetSecurityDescriptorDacl(g_jobSA.lpSecurityDescriptor, TRUE, NULL, FALSE);
	g_jobSA.nLength = sizeof(g_jobSA);
	g_jobSA.bInheritHandle = TRUE;

	g_job=CreateJobObject(&g_jobSA, _T("Global\\courseTaskManagerJob"));
	//_tcprintf(_T("Job exists: %d\n"), GetLastError()==ERROR_ALREADY_EXISTS);

	return (g_mutex!=NULL) && (g_job!=NULL);
}

bool startProcess(LPCTSTR a_commandLine)
{
	bool _retVal=false;
	DWORD _mutexState=WaitForSingleObject(g_mutex, INFINITE);
	if (_mutexState==WAIT_OBJECT_0 || _mutexState==WAIT_ABANDONED)
	{
		STARTUPINFO _startupInfo;
		//ZeroMemory(&_startupInfo,sizeof(STARTUPINFO));
		memset(&_startupInfo, 0, sizeof(_startupInfo));
		_startupInfo.cb = sizeof(_startupInfo);

		PROCESS_INFORMATION _processInformation;
		memset(&_processInformation, 0, sizeof(_processInformation));

		TCHAR _tempCmdLine[MAX_PATH * 2];  //Needed since CreateProcessW may change the contents of CmdLine
		_tcscpy_s(_tempCmdLine, MAX_PATH *2, a_commandLine);
		BOOL _result=CreateProcess(NULL, _tempCmdLine, NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS | DETACHED_PROCESS, NULL, NULL, &_startupInfo, &_processInformation);
		if (_result==TRUE)
		{
			_result=AssignProcessToJobObject(g_job, _processInformation.hProcess);
			CloseHandle(_processInformation.hProcess);
			CloseHandle(_processInformation.hThread);
			_retVal=_result==TRUE;
		}

		ReleaseMutex(g_mutex);
	}
	return _retVal;
}

bool list()
{
	bool _retVal=false;
	DWORD _mutexState=WaitForSingleObject(g_mutex, INFINITE);
	if (_mutexState==WAIT_OBJECT_0 || _mutexState==WAIT_ABANDONED)
	{
		JOBOBJECT_BASIC_PROCESS_ID_LIST _pidList;
		BOOL _queryResult=QueryInformationJobObject(g_job, JobObjectBasicProcessIdList, &_pidList, sizeof(_pidList), NULL);
		int _maxIds=max(_pidList.NumberOfAssignedProcesses, STANDARD_PROCESS_IN_LIST)+ADDITIONAL_PROCESS_IN_LIST;
		DWORD _requiredSize=sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST)+(_maxIds)*sizeof(DWORD);
		PJOBOBJECT_BASIC_PROCESS_ID_LIST _extendedPidList=(PJOBOBJECT_BASIC_PROCESS_ID_LIST)_alloca(_requiredSize);
		_extendedPidList->NumberOfAssignedProcesses=_maxIds;
		_queryResult=QueryInformationJobObject(g_job, JobObjectBasicProcessIdList, _extendedPidList, _requiredSize, NULL);
		if (_queryResult==TRUE)
		{
			TCHAR _path[MAX_PATH];

			_tcprintf(_T("Всего процессов: %d\n"), _extendedPidList->NumberOfProcessIdsInList);
			if (_extendedPidList->NumberOfProcessIdsInList>0)
			{
				_tcprintf(_T("%6s\t%s\n"), _T("PID"), _T("Процесс"));

				for (DWORD i=0; i<_extendedPidList->NumberOfProcessIdsInList; i++)
				{
					DWORD _pathSize=MAX_PATH;
					HANDLE _process=OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, _extendedPidList->ProcessIdList[i]);
					if (_process!=INVALID_HANDLE_VALUE)
					{
						BOOL _nameQueryResult=QueryFullProcessImageName(_process, 0, _path, &_pathSize);
						if (_nameQueryResult==TRUE)
						{
							_tcprintf(_T("%6d\t%s\n"), _extendedPidList->ProcessIdList[i], _path);
						} else
						{
							_tcprintf(_T("%6d\t%s\n"), _extendedPidList->ProcessIdList[i], _T("не удалось получить путь"));
						}
						CloseHandle(_process);
					} else
					{
						_tcprintf(_T("%6d\t%s\n"), _extendedPidList->ProcessIdList[i], _T("не удалось получить информацию"));
					}
				}
			}
			_retVal=true;
		}

		ReleaseMutex(g_mutex);
	}
	return _retVal;
}

bool killProcess(DWORD a_pid)
{
	bool _retVal=false;
	DWORD _mutexState=WaitForSingleObject(g_mutex, INFINITE);
	if (_mutexState==WAIT_OBJECT_0 || _mutexState==WAIT_ABANDONED)
	{
		HANDLE _process=OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, a_pid);
		if (_process!=INVALID_HANDLE_VALUE)
		{
			BOOL _isProcessInOurJob=FALSE;
			BOOL _execResult=IsProcessInJob(_process, g_job, &_isProcessInOurJob);
			if ((_execResult==TRUE) && (_isProcessInOurJob==TRUE))
			{
				BOOL _terminateResult=TerminateProcess(_process, -1);
				if (_terminateResult==TRUE)
				{
					_retVal=true;
					_tcprintf(_T("%6d успешно остановлен\n"), a_pid);
				} else
				{
					_tcprintf(_T("%6d не может быть остановлен\n"), a_pid);
				}
			} else
			{
				_tcprintf(_T("%6d запущен извне\n"), a_pid);
			}
		} else
		{
			_tcprintf(_T("%6d некорректный идентификатор\n"), a_pid);
		}

		ReleaseMutex(g_mutex);
	}
	return _retVal;
}

void exitTaskManager()
{
	isExit=true;
}

void deinitialize()
{
	free(g_mutexSA.lpSecurityDescriptor);
	free(g_jobSA.lpSecurityDescriptor);
}

void trim(tstring& a_string, const TCHAR* a_delimeters)
{
	a_string.erase(0, a_string.find_first_not_of(a_delimeters));
	a_string.erase(a_string.find_last_not_of(a_delimeters)+1, tstring::npos);
}

int _tmain(int argC, LPCTSTR argV[])
{
	setlocale( LC_ALL, ".866" );

	bool _initResult=initMutexAndJob();

	if (_initResult)
	{
		atexit(deinitialize);

		tstring _line=_T("");
		do
		{
			tcout<<_T(">");
			std::getline(tcin, _line);
			trim(_line, _T(" \t\r\n"));

			bool _commandResult=false, _isCommand=true;
			if (!_tcsnicoll(_line.c_str(), g_startPrefix, _tcslen(g_startPrefix)))
			{
				_line.erase(0, _tcslen(g_startPrefix));
				trim(_line, _T(" \t\r\n"));
				if (_tcslen(_line.c_str())>0)
				{
					_commandResult=startProcess(_line.c_str());
				} else
				{
					_isCommand=false;
				}
			} else
			{
				if (!_tcsnicoll(_line.c_str(), g_stopPrefix, _tcslen(g_stopPrefix)))
				{
					_line.erase(0, _tcslen(g_stopPrefix));
					trim(_line, _T(" \t\r\n"));
					if (_tcslen(_line.c_str())>0)
					{
						TCHAR *_endPtr;
						DWORD _pid=_tcstol(_line.c_str(), &_endPtr, 0);
						_commandResult=killProcess(_pid);
					} else
					{
						_isCommand=false;
					}
				} else
				{
					if (!_tcsicoll(_line.c_str(), g_listPrefix))
					{
						_commandResult=list();
					} else
					{
						if (!_tcsicoll(_line.c_str(), g_exitPrefix))
						{
							exitTaskManager();
						} else
						{
							_isCommand=false;
						}
					}
				}
			}
			if (_isCommand)
			{
				if (_commandResult)
				{
					tcout<<_T("команда выполнена успешно")<<std::endl;
				} else
				{
					tcout<<_T("выполнение команды прервано")<<std::endl;
				}
			} else
			{
				tcout<<_T("Доступные команды: ")<<std::endl;
				tcout<<_T("\tПУСК путь_к_приложению [передаваемые параметры]")<<std::endl;
				tcout<<_T("\tКОНЕЦ идентификатор_процесса")<<std::endl;
				tcout<<_T("\tСПИСОК")<<std::endl;
				tcout<<_T("\tВЫХОД")<<std::endl;
			}
		} while(!isExit);
	}
}