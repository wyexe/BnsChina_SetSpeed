#include <MyTools/CLProcess.h>
#include <MyTools/CLLock.h>
#include <MyTools/CLGrammar.h>
#include <MyTools/Character.h>
#include <memory>
#include <algorithm>
#include <iostream>
using namespace std;
#define _SELF L"main.cpp"

#define SZFILE_NAME_SHAREDINFO	L"Bns_SetSpeed_ShareName"
typedef struct _Bns_Game_Info
{
	BOOL	bExist;				// 是否被占用
	WCHAR	wszPlayerName[64];	// 角色名
	float	fSpeed;				// 速度
	DWORD	dwPid;				// 进程ID
	BOOL	bKeepAlive;			// 心跳
}Bns_Game_Info, *PBns_Game_Info;

typedef struct _Bns_Share_Info
{
	Bns_Game_Info GameArray[20];
}Bns_Share_Info, *PBns_Share_Info;
PBns_Share_Info g_pSharedInfo = NULL;
MyTools::CLLock ShareInfoLock(L"ShareInfoLock");

// 获取所有命令
DWORD WINAPI GetHelpCmd(vector<MyTools::GrammarContext>& vlst, LPWSTR pwszRetMsg)
{
	wcout << L"GetPlayerName 遍历角色名" << endl;
	wcout << L"SetPlayerSpeed -Name||-ALL -Speed" << endl;
	wcout << L"ExitGame -Name||-ALL" << endl;
	return 0;
}

// 获取角色名称(在保存的进程上)
DWORD WINAPI GetPlayerName(vector<MyTools::GrammarContext>& vlst, LPWSTR pwszRetMsg)
{
	for (int i = 0; i < 20; ++i)
	{
		auto pBns_Game_Info = &g_pSharedInfo->GameArray[i];
		if (!pBns_Game_Info->bExist)
			continue;

		wcout << L"角色名:" << pBns_Game_Info->wszPlayerName << endl;
	}
	return 0;
}

BOOL SetSomething_ALLPlayer(std::function<BOOL(Bns_Game_Info&)> fCondiction, std::function<void(Bns_Game_Info&)> f)
{
	for (int i = 0; i < 20; ++i)
	{
		Bns_Game_Info& Player = g_pSharedInfo->GameArray[i];
		if (fCondiction(Player))
			f(Player);
	}
	return TRUE;
}

// 设置走路速度
DWORD WINAPI SetPlayerSpeed(vector<MyTools::GrammarContext>& vlst, LPWSTR pwszRetMsg)
{
	// 获取设置的速度
	float fSpeed = MyTools::CLGrammar::Query_FLOATParm_By_GrammarList(2, vlst);
	// 获取角色名
	wstring wsPlayerName = MyTools::CLGrammar::Query_LPWSTRParm_By_GrammarList(1, vlst);
	if (wsPlayerName == L"ALL")
	{
		ShareInfoLock.Access([&fSpeed]{
			SetSomething_ALLPlayer([](Bns_Game_Info& Player){ return Player.bExist; }, [&fSpeed](Bns_Game_Info& Player){
				Player.fSpeed = fSpeed;
			});
		});
		
		
		return 0;
	}

	ShareInfoLock.Access([&wsPlayerName, &fSpeed]{
		SetSomething_ALLPlayer([&wsPlayerName](Bns_Game_Info& Player){
			return Player.bExist && MyTools::CCharacter::wstrcmp_my(wsPlayerName.c_str(), Player.wszPlayerName); },
				[&fSpeed](Bns_Game_Info& Player){
				Player.fSpeed = fSpeed;
			});
	});
	

	return 0;
}

// 退出
DWORD WINAPI ExitGame(vector<MyTools::GrammarContext>& vlst, LPWSTR pwszRetMsg)
{
	// 获取角色名
	wstring wsPlayerName = MyTools::CLGrammar::Query_LPWSTRParm_By_GrammarList(1, vlst);
	if (wsPlayerName == L"ALL")
	{
		ShareInfoLock.Access([]{
			SetSomething_ALLPlayer([](Bns_Game_Info& Player){ return Player.bExist; }, [](Bns_Game_Info& Player){
				MyTools::CLProcess::TerminateProc_For_ProcId(Player.dwPid);
				ZeroMemory(&Player, sizeof(Bns_Game_Info));
			});
		});
		return 0;
	}

	ShareInfoLock.Access([&wsPlayerName]{
		SetSomething_ALLPlayer([&wsPlayerName](Bns_Game_Info& Player){
			return Player.bExist && MyTools::CCharacter::wstrcmp_my(wsPlayerName.c_str(), Player.wszPlayerName); },
				[](Bns_Game_Info& Player){
				MyTools::CLProcess::TerminateProc_For_ProcId(Player.dwPid);
				ZeroMemory(&Player, sizeof(Bns_Game_Info));
			});
	});
	
	return 0;
}

BOOL InitMapView()
{
	wchar_t szFilePath[MAX_PATH] = { 0 };
	wsprintf(szFilePath, L"%s.SharedMem", SZFILE_NAME_SHAREDINFO);
	HANDLE hFileSharedInfo = CreateFile(szFilePath, GENERIC_READ | GENERIC_WRITE,          // open for reading
		FILE_SHARE_READ | FILE_SHARE_WRITE,       // share for reading
		NULL,                  // default security
		CREATE_ALWAYS,         // existing file only
		FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, // normal file
		NULL);
	if (hFileSharedInfo == NULL)
	{
		wcout << L"创建映射文件失败! 自己反省一下!" << endl;
		return FALSE;
	}

	HANDLE hFileMap = CreateFileMapping(hFileSharedInfo, NULL, PAGE_READWRITE, 0, sizeof(Bns_Share_Info), SZFILE_NAME_SHAREDINFO);
	if (hFileMap == NULL)
	{
		wcout << L"创建文件映射失败! 自己反省一下!" << endl;
		::CloseHandle(hFileSharedInfo);
		return FALSE;
	}
	::CloseHandle(hFileSharedInfo);
	
	g_pSharedInfo = (PBns_Share_Info)::MapViewOfFile(hFileMap, FILE_MAP_READ | FILE_MAP_WRITE, NULL, NULL, sizeof(Bns_Share_Info));
	if (g_pSharedInfo == NULL)
	{
		wcout << L"映射内存失败! 自己反省一下!" << endl;
		::CloseHandle(hFileMap);
		return FALSE;
	}

	ZeroMemory(g_pSharedInfo, sizeof(Bns_Share_Info));
	return TRUE;
}

HANDLE hCheckLoseConnectThread = NULL;
DWORD WINAPI _CheckLoseConnectThread()
{
	while (true)
	{
		ShareInfoLock.Access([]{
			ShareInfoLock.Access([]{
				SetSomething_ALLPlayer([](Bns_Game_Info& Player){ return Player.bExist; }, [](Bns_Game_Info& Player){
					Player.bKeepAlive = FALSE;
				});
			});
		});

		Sleep(10 * 1000);

		ShareInfoLock.Access([]{
			ShareInfoLock.Access([]{
				SetSomething_ALLPlayer([](Bns_Game_Info& Player){ return Player.bExist; }, [](Bns_Game_Info& Player){
					if (Player.bKeepAlive == FALSE)
					{
						//CPrintLog::PrintLog_W(_SELF, __LINE__, L"角色:%s 进程ID:%X 掉线了, 回收内存", Player.wszPlayerName, Player.dwPid);
						MyTools::CLProcess::TerminateProc_For_ProcId(Player.dwPid);
						ZeroMemory(&Player, sizeof(Bns_Game_Info));
					}
				});
			});
		});
	}
	return 0;
}

DWORD WINAPI _RemoteInjectorThread(LPVOID lpParm)
{
	vector<DWORD> vlst;
	auto fnExistList = [&vlst](__in DWORD dwPid)
	{
		return std::find_if(vlst.begin(), vlst.end(), [&dwPid](const DWORD dwProcId){
			return dwPid == dwProcId;
		}) != vlst.end();
	};

	while (true)
	{
		Sleep(1000);

		vector<PROCESSENTRY32> vProcess;
		MyTools::CLProcess::GetProcessSnapshot(vProcess);

		std::for_each(vProcess.begin(), vProcess.end(), [&vlst, &fnExistList](const PROCESSENTRY32& PROCESSENTRY32_){
			if (MyTools::CCharacter::wstrstr_my(PROCESSENTRY32_.szExeFile,L"Client.exe"))
			{
				if (!fnExistList(PROCESSENTRY32_.th32ProcessID))
				{
					WCHAR wszDLLPath[MAX_PATH] = { 0 };
					::GetCurrentDirectoryW(MAX_PATH, wszDLLPath);
					lstrcatW(wszDLLPath, L"\\BnsChina_SetSpeed_DLL.dll");
					//CPrintLog::PrintLog_W(_SELF,__LINE__,L"注入DLL, PID=%X", PROCESSENTRY32_.th32ProcessID);
					BOOL bSuc = MyTools::CLProcess::LoadRemoteDLL(PROCESSENTRY32_.th32ProcessID, wszDLLPath);
					MessageBoxW(nullptr, bSuc ? L"YES" : L"No", L"", NULL);
					vlst.push_back(PROCESSENTRY32_.th32ProcessID);
				}
			}
		});

		for (int i = 0;i < (int)vlst.size(); ++i)
		{
			if (!MyTools::CLProcess::Is_Exist_Process_For_ProcId(vlst.at(i)))
				vlst.erase(vlst.begin() + i--);
		}
	}

	return 0;
}

int main()
{
	MyTools::CLGrammar::AddTranList(L"?",				GetHelpCmd);
	MyTools::CLGrammar::AddTranList(L"GetPlayerName",	GetPlayerName);
	MyTools::CLGrammar::AddTranList(L"SetPlayerSpeed",	SetPlayerSpeed);
	MyTools::CLGrammar::AddTranList(L"ExitGame",			ExitGame);

	InitMapView();
	::CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)_CheckLoseConnectThread, NULL, NULL, NULL);
	::CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)_RemoteInjectorThread, NULL, NULL, NULL);

	MyTools::CCharacter::SetConsoleLanguage();
	MyTools::CCharacter::SetSpecialCharacterMode();
	while (true)
	{
		WCHAR wszCmd[1024] = { 0 };
		wcout << L"1>  ";
		wcin.getline(wszCmd, 1024);

		WCHAR wszErrorMsg[1024] = { 0 };
		MyTools::CLGrammar::AnalysisGrammar(wszCmd, wszErrorMsg);
		wcout << L"Tip:" << wszErrorMsg << endl;
	}
	

	return 0;
}