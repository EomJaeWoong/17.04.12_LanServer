#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mstcpip.h>
#include <process.h>
#include <stdio.h>

#pragma comment (lib, "Winmm.lib")
#pragma comment (lib, "Ws2_32.lib")

#include "Config.h"
#include "StreamQueue.h"
#include "NPacket.h"
#include "LanServer.h"

CLanServer::CLanServer()
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
		Session[iCnt]._bUsed = false;

	_bShutdown = false;
	_iSessionID = 0;
}

CLanServer::~CLanServer()
{

}

bool CLanServer::Start(WCHAR *wOpenIP, int iPort, int iWorkerThdNum, BOOL bNagle, int iMaxConnection)
{
	int retval;
	DWORD dwThreadID;

	_bShutdown = false;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// 윈속 초기화
	//////////////////////////////////////////////////////////////////////////////////////////////////
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Completion Port 생성
	//////////////////////////////////////////////////////////////////////////////////////////////////
	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hIOCP == NULL)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// socket 생성
	//////////////////////////////////////////////////////////////////////////////////////////////////
	listen_sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listen_sock == INVALID_SOCKET)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//bind
	//////////////////////////////////////////////////////////////////////////////////////////////////
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(iPort);
	InetPton(AF_INET, wOpenIP, &serverAddr.sin_addr);
	retval = bind(listen_sock, (SOCKADDR *)&serverAddr, sizeof(SOCKADDR_IN));
	if (retval == SOCKET_ERROR)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//listen
	//////////////////////////////////////////////////////////////////////////////////////////////////
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// nagle 옵션
	//////////////////////////////////////////////////////////////////////////////////////////////////
	_bNagle = bNagle;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Thread 생성
	//////////////////////////////////////////////////////////////////////////////////////////////////
	hAcceptThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		AcceptThread,
		this,
		0,
		(unsigned int *)&dwThreadID
		);

	_iWorkerThdNum = iWorkerThdNum;

	for (int iCnt = 0; iCnt < iWorkerThdNum; iCnt++)
	{
		hWorkerThread[iCnt] = (HANDLE)_beginthreadex(
			NULL,
			0,
			WorkerThread,
			this,
			0,
			(unsigned int *)&dwThreadID
			);
	}

	hMonitorThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		MonitorThread,
		this,
		0,
		(unsigned int *)&dwThreadID
		);

	return TRUE;
}

void CLanServer::Stop()
{
	int retval;

	_bShutdown = true;

	HANDLE *hThread = new HANDLE[1 + _iWorkerThdNum];
	hThread[0] = hAcceptThread;
	for (int iCnt = 0; iCnt < _iWorkerThdNum; iCnt++)
		hThread[iCnt + 1] = hWorkerThread[iCnt];

	retval = WaitForMultipleObjects(1 + _iWorkerThdNum, hThread, TRUE, INFINITE);
	if (retval != WAIT_OBJECT_0)
		wprintf(L"Stop Error\n");

	CloseHandle(hIOCP);

	WSACleanup();
}

int CLanServer::GetClientCount(){ return _iSessionCount; }

//-------------------------------------------------------------------------------------
// Packet
//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------
// 패킷 보내기
//-------------------------------------------------------------------------------------
bool CLanServer::SendPacket(__int64 iSessionID, CNPacket *pPacket)
{
	int iCnt;

	for (iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		if (Session[iCnt]._iSessionID == iSessionID)
		{
			pPacket->addRef();
			Session[iCnt].SendQ.Put((char *)&pPacket, sizeof(pPacket));
			break;
		}
	}

	SendPost(&Session[iCnt]);

	return true;
}

//-------------------------------------------------------------------------------------
// Recv 등록
//-------------------------------------------------------------------------------------
void CLanServer::RecvPost(SESSION *pSession)
{
	int retval;
	DWORD dwRecvSize, dwflag = 0;
	WSABUF wBuf;

	wBuf.buf = pSession->RecvQ.GetWriteBufferPtr();
	wBuf.len = pSession->RecvQ.GetNotBrokenPutSize();

	memset(&(pSession->_RecvOverlapped), 0, sizeof(OVERLAPPED));

	InterlockedIncrement64((LONG64 *)&(pSession->_lIOCount));
	retval = WSARecv(pSession->_SessionInfo._socket, &wBuf, 1, &dwRecvSize, &dwflag, &pSession->_RecvOverlapped, NULL);

	if (retval == SOCKET_ERROR)
	{
		int iErrorCode = GetLastError();
		if (iErrorCode != WSA_IO_PENDING)
		{
			OnError(iErrorCode, L"RecvPost Error\n");
			if (0 == InterlockedDecrement64((LONG64 *)&(pSession->_lIOCount)))
				ReleaseSession(pSession);

			return;
		}
	}
}

//-------------------------------------------------------------------------------------
// Send 등록
//-------------------------------------------------------------------------------------
BOOL CLanServer::SendPost(SESSION *pSession)
{
	int retval, iCount = 0;
	DWORD dwSendSize, dwflag = 0;
	WSABUF wBuf[MAX_WSABUF];

	while (pSession->SendQ.GetUseSize() / sizeof(char *) > iCount)
	{
		if (iCount >= MAX_WSABUF)
			return false;

		CNPacket *pPacket = NULL;
		pSession->SendQ.Peek((char *)&pPacket, iCount * sizeof(char *), sizeof(char *));

		wBuf[iCount].buf = (char *)pPacket->GetBufferPtr();
		wBuf[iCount].len = pPacket->GetPacketSize();
	
		InterlockedIncrement64((LONG64 *)&_SendPacketCounter);
		iCount++;
	}

	if (pSession->_bSendFlag == TRUE)	return FALSE;

	else{
		InterlockedIncrement64((LONG64 *)&pSession->_lIOCount);
		pSession->_bSendFlag = TRUE;
		retval = WSASend(pSession->_SessionInfo._socket, wBuf, iCount, &dwSendSize, dwflag, &pSession->_SendOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			int iErrorCode = GetLastError();
			if (iErrorCode != WSA_IO_PENDING)
			{
				OnError(iErrorCode, L"SendPost Error\n");
				if (0 == InterlockedDecrement64((LONG64 *)&pSession->_lIOCount))
					ReleaseSession(pSession);

				return FALSE;
			}
		}

		pSession->SendQ.RemoveData(sizeof(char *) * iCount);
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------
// Thread
//-------------------------------------------------------------------------------------
int CLanServer::WorkerThread_Update()
{
	int retval;
	CNPacket* pPacket;

	while (!_bShutdown)
	{
		DWORD dwTransferred = 0;
		OVERLAPPED *pOverlapped = NULL;
		SESSION *pSession = NULL;

		retval = GetQueuedCompletionStatus(hIOCP, &dwTransferred, (PULONG_PTR)&pSession,
			(LPOVERLAPPED *)&pOverlapped, INFINITE);

		//----------------------------------------------------------------------------
		// Error, 종료 처리
		//----------------------------------------------------------------------------
		// IOCP 에러 서버 종료
		if (retval == FALSE && (pOverlapped == NULL || pSession == NULL))
		{
			int iErrorCode = WSAGetLastError();
			OnError(iErrorCode, L"IOCP HANDLE Error\n");

			break;
		}

		// 워커스레드 정상 종료
		else if (dwTransferred == 0 && pSession == NULL && pOverlapped == NULL)
		{
			OnError(0, L"Worker Thread Done.\n");
			PostQueuedCompletionStatus(hIOCP, 0, NULL, NULL);
			return 0;
		}

		//----------------------------------------------------------------------------
		// 정상종료
		// 클라이언트 에서 closesocket() 혹은 shutdown() 함수를 호출한 시점
		//----------------------------------------------------------------------------
		else if (dwTransferred == 0)
		{
			if (pOverlapped == &(pSession->_RecvOverlapped))
			{
			}

			else if (pOverlapped == &(pSession->_SendOverlapped))
				pSession->_bSendFlag = false;
				
			retval = InterlockedDecrement64((LONG64 *)&pSession->_lIOCount);
			if (0 == retval)
				ReleaseSession(pSession);

			continue;
		}
		//----------------------------------------------------------------------------

		OnWorkerThreadBegin();

		//////////////////////////////////////////////////////////////////////////////
		// Recv 완료
		//////////////////////////////////////////////////////////////////////////////
		if (pOverlapped == &pSession->_RecvOverlapped)
		{
			pPacket = CNPacket::Alloc();
			pSession->RecvQ.MoveWritePos(dwTransferred);
			pPacket->Put(pSession->RecvQ.GetReadBufferPtr(), pSession->RecvQ.GetUseSize());
			pSession->RecvQ.RemoveData(dwTransferred);

			OnRecv(pSession->_iSessionID, pPacket);
			
			pPacket->Free();

			RecvPost(pSession);
		}

		//////////////////////////////////////////////////////////////////////////////
		// Send 완료
		//////////////////////////////////////////////////////////////////////////////
		else if (pOverlapped == &pSession->_SendOverlapped)
		{
			pSession->_bSendFlag = FALSE;

			OnSend(pSession->_iSessionID, dwTransferred);
		}

		retval = InterlockedDecrement64((LONG64 *)&pSession->_lIOCount);
		if (0 == retval)
			ReleaseSession(pSession);

		else if(0 < pSession->_lIOCount)
			//Count가 0보다 작으면 크래쉬 내기

		OnWorkerThreadEnd();
	}

	return 0;
}

int CLanServer::AcceptThread_Update()
{
	HANDLE retval;

	SOCKET ClientSocket;
	int addrlen = sizeof(SOCKADDR_IN);
	SOCKADDR_IN clientSock;
	WCHAR clientIP[16];

	while (!_bShutdown)
	{
		ClientSocket = WSAAccept(listen_sock, (SOCKADDR *)&clientSock, &addrlen, NULL, NULL);

		if (ClientSocket == INVALID_SOCKET)
		{
			DisconnectSession(ClientSocket);
			continue;
		}
		InetNtop(AF_INET, &clientSock.sin_addr, clientIP, 16);

		if (_iSessionCount >= MAX_SESSION)
			OnError(dfMAX_SESSION, L"Session is Maximun!");

		if (!OnConnectionRequest(clientIP, ntohs(clientSock.sin_port)))		// accept 직후
		{
			DisconnectSession(ClientSocket);
			continue;
		}	
		InterlockedIncrement64((LONG64 *)&_AcceptCounter);
		InterlockedIncrement64((LONG64 *)&_AcceptTotalCounter);

		//////////////////////////////////////////////////////////////////////////////
		// 세션 추가 과정
		//////////////////////////////////////////////////////////////////////////////
		for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
		{
			// 빈 세션
			if (!Session[iCnt]._bUsed)
			{
				/////////////////////////////////////////////////////////////////////
				// 세션 초기화
				/////////////////////////////////////////////////////////////////////
				wcscpy_s(Session[iCnt]._SessionInfo._IP, 16, clientIP);
				Session[iCnt]._SessionInfo._iPort = ntohs(clientSock.sin_port);

				/////////////////////////////////////////////////////////////////////
				// KeepAlive
				/////////////////////////////////////////////////////////////////////
				tcp_keepalive tcpkl;

				tcpkl.onoff = 1;
				tcpkl.keepalivetime = 3000; //30초 개발할땐 짧게 라이브땐 2~30초
				tcpkl.keepaliveinterval = 2000; //  keepalive 신호

				DWORD dwReturnByte;
				WSAIoctl(ClientSocket, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0,
					0, &dwReturnByte, NULL, NULL);
				/////////////////////////////////////////////////////////////////////

				Session[iCnt]._SessionInfo._socket = ClientSocket;

				Session[iCnt]._iSessionID = InterlockedIncrement64((LONG64 *)&_iSessionID);

				Session[iCnt].RecvQ.ClearBuffer();
				Session[iCnt].SendQ.ClearBuffer();

				memset(&(Session[iCnt]._RecvOverlapped), 0, sizeof(OVERLAPPED));
				memset(&(Session[iCnt]._SendOverlapped), 0, sizeof(OVERLAPPED));

				Session[iCnt]._bSendFlag = FALSE;
				Session[iCnt]._lIOCount = 0;

				retval = CreateIoCompletionPort((HANDLE)Session[iCnt]._SessionInfo._socket,
					hIOCP, 
					(ULONG_PTR)&Session[iCnt], 
					0);

				if (!retval)
					continue;
				
				Session[iCnt]._bUsed = true;
				
				OnClientJoin(&Session[iCnt]._SessionInfo, Session[iCnt]._iSessionID);
				RecvPost(&Session[iCnt]);

				InterlockedIncrement64((LONG64 *)&_iSessionCount);
				break;
			}
		}
	}

	return 0;
}

int CLanServer::MonitorThread_Update()
{
	DWORD iGetTime = timeGetTime();

	while (1)
	{
		if (timeGetTime() - iGetTime < 1000)
			continue;

		_AcceptTPS = _AcceptCounter;
		_AcceptTotalTPS += _AcceptTotalCounter;
		_RecvPacketTPS = _RecvPacketCounter;
		_SendPacketTPS = _SendPacketCounter;

		_AcceptCounter = 0;
		_AcceptTotalCounter = 0;
		_RecvPacketCounter = 0;
		_SendPacketCounter = 0;

		iGetTime = timeGetTime();
	}
}

unsigned __stdcall CLanServer::WorkerThread(LPVOID workerArg)
{
	return ((CLanServer *)workerArg)->WorkerThread_Update();
}

unsigned __stdcall CLanServer::AcceptThread(LPVOID acceptArg)
{
	return ((CLanServer *)acceptArg)->AcceptThread_Update();
}

unsigned __stdcall CLanServer::MonitorThread(LPVOID monitorArg)
{
	return ((CLanServer *)monitorArg)->MonitorThread_Update();
}

//-------------------------------------------------------------------------------------
// Disconnect
//-------------------------------------------------------------------------------------
void CLanServer::DisconnectSession(SOCKET socket)
{
	shutdown(socket, SD_BOTH);
}

void CLanServer::DisconnectSession(SESSION *pSession)
{
	DisconnectSession(pSession->_SessionInfo._socket);
}

void CLanServer::DisconnectSession(__int64 iSessionID)
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		if (Session[iCnt]._iSessionID == iSessionID)
		{
			DisconnectSession(&Session[iCnt]);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------
// Release
//-------------------------------------------------------------------------------------
void CLanServer::ReleaseSession(SESSION *pSession)
{
	DisconnectSession(pSession);

	if (pSession->RecvQ.GetUseSize() != 0 ||
		pSession->SendQ.GetUseSize() != 0)
		return;

	pSession->_bUsed = false;
	InterlockedDecrement64((LONG64 *)&_iSessionCount);
}

void CLanServer::ReleaseSession(__int64 iSessionID)
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		if (Session[iCnt]._iSessionID == iSessionID)
		{
			ReleaseSession(&Session[iCnt]);
			break;
		}
	}
}