#include <Windows.h>
#include <stdio.h>
#include <conio.h>

#include "Config.h"
#include "StreamQueue.h"
#include "NPacket.h"
#include "LanServer.h"
#include "LanServerTest.h"

CLanServerTest LanServer;

void main()
{
	char chControlKey;

	if (!LanServer.Start(SERVER_IP, SERVER_PORT, 1, false, 100))
		return;

	while (1)
	{
		wprintf(L"------------------------------------------------\n");
		wprintf(L"Connect Session : %d\n", LanServer._iSessionCount);
		wprintf(L"Accept TPS : %d\n", LanServer._AcceptTPS);
		wprintf(L"Accept Total : %d\n", LanServer._AcceptTotalTPS);
		wprintf(L"RecvPacket TPS : %d\n", LanServer._RecvPacketTPS);
		wprintf(L"SendPacket TPS : %d\n", LanServer._SendPacketTPS);
		wprintf(L"PacketPool Use : %d\n", 0);
		wprintf(L"PacketPool Alloc : %d\n", 0);
		wprintf(L"------------------------------------------------\n\n");

		Sleep(999);

		if (_kbhit() != 0){
			chControlKey = _getch();
			if (chControlKey == 'q' || chControlKey == 'Q')
			{
				//------------------------------------------------
				// 辆丰贸府
				//------------------------------------------------
				break;
			}

			else if (chControlKey == 'G' || chControlKey == 'g')
			{
				//------------------------------------------------
				// 矫累贸府
				//------------------------------------------------
				LanServer.Start(SERVER_IP, SERVER_PORT, 1, false, 100);
			}

			else if (chControlKey == 'S' || chControlKey == 's')
			{
				//------------------------------------------------
				// 沥瘤贸府
				//------------------------------------------------
				LanServer.Stop();
			}
		}
	}
}