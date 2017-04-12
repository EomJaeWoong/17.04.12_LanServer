#include <WinSock2.h>
#include <WS2tcpip.h>
#include <process.h>
#include <stdio.h>

#pragma comment (lib, "Ws2_32.lib")

#include "StreamQueue.h"
#include "NPacket.h"
#include "LanServer.h"

CLanServer::CLanServer()
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
		Session[iCnt]._bUsed = false;

	_bShutdown = false;
}

CLanServer::~CLanServer()
{

}

bool CLanServer::Start(WCHAR *wOpenIP, int iPort, int iWorkerThdNum, BOOL bNagle, int iMaxConnection)
{
	int retval;
	DWORD dwThreadID;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// ���� �ʱ�ȭ
	//////////////////////////////////////////////////////////////////////////////////////////////////
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Completion Port ����
	//////////////////////////////////////////////////////////////////////////////////////////////////
	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hIOCP == NULL)
		return FALSE;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// socket ����
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
	// nagle �ɼ�
	//////////////////////////////////////////////////////////////////////////////////////////////////


	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Thread ����
	//////////////////////////////////////////////////////////////////////////////////////////////////
	hAcceptThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		AcceptThread,
		this,
		0,
		(unsigned int *)&dwThreadID
		);

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

}

int CLanServer::GetClientCount(){ return _iSessionCount; }

//-------------------------------------------------------------------------------------
// Packet
//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------
// ��Ŷ ������
//-------------------------------------------------------------------------------------
bool CLanServer::SendPacket(__int64 iSessionID, CNPacket *pPacket)
{
	Session[iSessionID].SendQ.Put((char *)pPacket->GetHeaderBufferPtr(), pPacket->GetPacketSize());
	SendPost(&Session[iSessionID]);
	InterlockedIncrement64((LONG64 *)&_SendPacketCounter);

	return true;
}

//-------------------------------------------------------------------------------------
// Recv ���
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
				//Session Release

			return;
		}
	}
}

//-------------------------------------------------------------------------------------
// Send ���
//-------------------------------------------------------------------------------------
BOOL CLanServer::SendPost(SESSION *pSession)
{
	int retval, iCount = 0;
	DWORD dwSendSize, dwflag = 0;
	WSABUF wBuf;

	wBuf.buf = pSession->SendQ.GetReadBufferPtr();
	wBuf.len = pSession->SendQ.GetUseSize();

	if (pSession->_bSendFlag == TRUE)	return FALSE;

	else{
		InterlockedIncrement64((LONG64 *)&pSession->_lIOCount);
		pSession->_bSendFlag = TRUE;
		retval = WSASend(pSession->_SessionInfo._socket, &wBuf, 1, &dwSendSize, dwflag, &pSession->_SendOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			int iErrorCode = GetLastError();
			if (iErrorCode != WSA_IO_PENDING)
			{
				OnError(iErrorCode, L"SendPost Error\n");
				if (0 == InterlockedDecrement64((LONG64 *)&pSession->_lIOCount))
					//Session Release

				return FALSE;
			}
		}
	}
	
	return TRUE;
}

bool CLanServer::PacketProc(SESSION *pSession, CNPacket *pPacket)
{
	while (pSession->RecvQ.GetUseSize() > 0)
	{
		short header;

		//////////////////////////////////////////////////////////////////////////////////////////
		// RecvQ �뷮�� header���� ������ �˻�
		//////////////////////////////////////////////////////////////////////////////////////////
		if (pSession->RecvQ.GetUseSize() < sizeof(header))
			return FALSE;

		pSession->RecvQ.Peek((char *)&header, sizeof(header));

		//////////////////////////////////////////////////////////////////////////////////////////
		//header + payload �뷮�� RecvQ�뷮���� ū�� �˻�
		//////////////////////////////////////////////////////////////////////////////////////////
		if (pSession->RecvQ.GetUseSize() < header + sizeof(header))
			return FALSE;

		*pPacket << header;
		pSession->RecvQ.RemoveData(sizeof(header));

		//////////////////////////////////////////////////////////////////////////////////////////
		//payload�� cPacket�� �̰� ������ �˻�
		//////////////////////////////////////////////////////////////////////////////////////////
		if (header != pPacket->Put(pSession->RecvQ.GetReadBufferPtr(), header))
			return FALSE;

		pSession->RecvQ.RemoveData(header);		
		InterlockedIncrement64((LONG64 *)&_RecvPacketCounter);
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
		// Error, ���� ó��
		//----------------------------------------------------------------------------
		// IOCP ���� ���� ����
		if (retval == FALSE && (pOverlapped == NULL || pSession == NULL))
		{
			int iErrorCode = WSAGetLastError();
			OnError(iErrorCode, L"IOCP HANDLE Error\n");

			break;
		}

		// ��Ŀ������ ���� ����
		else if (dwTransferred == 0 && pSession == NULL && pOverlapped == NULL)
		{
			OnError(0, L"Worker Thread Done.\n");
			//pqcs �ֱ�
			return 0;
		}

		// ��������
		else if (dwTransferred == 0)
		{
			if (pOverlapped == &(pSession->_RecvOverlapped)){}

			else if (pOverlapped == &(pSession->_SendOverlapped))
			{
				pSession->_bSendFlag = false;
			}

			
			return 0;
		}
		//----------------------------------------------------------------------------

		OnWorkerThreadBegin();

		//////////////////////////////////////////////////////////////////////////////
		// Recv �Ϸ�
		//////////////////////////////////////////////////////////////////////////////
		if (pOverlapped == &pSession->_RecvOverlapped)
		{
			pPacket = new CNPacket();
			pSession->RecvQ.MoveWritePos(dwTransferred);
			
			PacketProc(pSession, pPacket);
			OnRecv(pSession->_iSessionID, pPacket);
		
			delete pPacket;

			RecvPost(pSession);
		}

		//////////////////////////////////////////////////////////////////////////////
		// Send �Ϸ�
		//////////////////////////////////////////////////////////////////////////////
		else if (pOverlapped == &pSession->_SendOverlapped)
		{
			pSession->SendQ.RemoveData(dwTransferred);
			pSession->_bSendFlag = FALSE;

			OnSend(pSession->_iSessionID, dwTransferred);
			
		}

		//Session Release
		// ����� ������ ���ϰ��� ���� �����ϰ� if���� �ֱ�
		if (0 == InterlockedDecrement64((LONG64 *)&pSession->_lIOCount))
			//Session Release

		//Count�� 0���� ������ ũ���� ����

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
		//ClientSocket = accept(listen_sock, (SOCKADDR *)&clientSock, &addrlen);
		ClientSocket = WSAAccept(listen_sock, (SOCKADDR *)&clientSock, &addrlen, NULL, NULL);

		if (ClientSocket == INVALID_SOCKET)
		{
			// ���� ����
			continue;
		}
		InetNtop(AF_INET, &clientSock.sin_addr, clientIP, 16);

		if (!OnConnectionRequest(clientIP, ntohs(clientSock.sin_port)))		// accept ����
		{
			// ���� ����
			continue;
		}	
		InterlockedIncrement64((LONG64 *)&_AcceptCounter);

		//////////////////////////////////////////////////////////////////////////////
		// ���� �߰� ����
		//////////////////////////////////////////////////////////////////////////////
		for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
		{
			// �� ����
			if (!Session[iCnt]._bUsed)
			{
				/////////////////////////////////////////////////////////////////////
				// ���� �ʱ�ȭ
				/////////////////////////////////////////////////////////////////////
				wcscpy_s(Session[iCnt]._SessionInfo._IP, 16, clientIP);
				Session[iCnt]._SessionInfo._iPort = ntohs(clientSock.sin_port);
				Session[iCnt]._SessionInfo._socket = ClientSocket;

				Session[iCnt]._iSessionID = iCnt;

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
				//InterlockedIncrement64((LONG64 *)&Session[iCnt]._lIOCount);
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
	while (1)
	{
		_AcceptTPS = _AcceptCounter;
		_RecvPacketTPS = _RecvPacketCounter;
		_SendPacketTPS = _SendPacketCounter;

		_AcceptCounter = 0;
		_RecvPacketCounter = 0;
		_SendPacketCounter = 0;

		wprintf(L"------------------------------------------------\n");
		wprintf(L"Connect Session : %d\n", _iSessionCount);
		wprintf(L"Accept TPS : %d\n", _AcceptTPS);
		wprintf(L"RecvPacket TPS : %d\n", _RecvPacketTPS);
		wprintf(L"SendPacket TPS : %d\n", _SendPacketTPS);
		wprintf(L"PacketPool Use : %d\n", 0);
		wprintf(L"PacketPool Alloc : %d\n", 0);
		wprintf(L"------------------------------------------------\n\n");

		Sleep(999);
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