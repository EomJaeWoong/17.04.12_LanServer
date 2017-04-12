#include <WinSock2.h>
#include <stdio.h>

#include "NPacket.h"
#include "StreamQueue.h"
#include "Config.h"
#include "LanServer.h"
#include "LanServerTest.h"

CLanServerTest::CLanServerTest()
	: CLanServer(){}

CLanServerTest::~CLanServerTest(){}

void CLanServerTest::OnClientJoin(SESSION_INFO *pSessionInfo, __int64 ClientID)		// Accept �� ����ó�� �Ϸ� �� ȣ��.
{
	
}

void CLanServerTest::OnClientLeave(__int64 ClientID)   					// Disconnect �� ȣ��
{

}

bool CLanServerTest::OnConnectionRequest(WCHAR *ClientIP, int Port)		// accept ����
{
	return true;
}

void CLanServerTest::OnRecv(__int64 ClientID, CNPacket *pPacket)			// ��Ŷ ���� �Ϸ� ��
{
	SendPacket(ClientID, pPacket);
}

void CLanServerTest::OnSend(__int64 ClientID, int sendsize)				// ��Ŷ �۽� �Ϸ� ��
{

}

void CLanServerTest::OnWorkerThreadBegin()								// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ��
{

}

void CLanServerTest::OnWorkerThreadEnd()								// ��Ŀ������ 1���� ���� ��
{

}

void CLanServerTest::OnError(int errorCode, WCHAR *errorString)
{
	wprintf(L"ErrorCode : %d, ErrorMsg : %s\n", errorCode, errorString);
}