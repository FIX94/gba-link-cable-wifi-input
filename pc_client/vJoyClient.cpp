/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "stdafx.h"
#include "public.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <cstdint>
#include "vjoyinterface.h"

//a,b,c,d,right,left,up,down,r,l
static const short bState[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };

#pragma comment(lib, "Ws2_32.lib")

int recvfromTimeOutUDP(SOCKET socket, long sec, long usec)
{
	// Setup timeval variable
	struct timeval timeout;
	struct fd_set fds;

	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	// Setup fd_set structure
	FD_ZERO(&fds);
	FD_SET(socket, &fds);

	return select(0, &fds, 0, 0, &timeout);
}
#ifdef _WIN64
static const char *arch = "x64";
#else
static const char *arch = "x86";
#endif

int __cdecl _tmain(int argc, _TCHAR* argv[])
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		_tprintf("Failed. Error Code : %d", WSAGetLastError());
		return 1;
	}

	struct sockaddr_in serv_addr;
	int serv_len = sizeof(serv_addr);
	uint16_t buffer;

	SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		_tprintf("ERROR opening socket");
		return 2;
	}

	_tprintf("GBA Link Cable WiFi Input Client v1.1 (%s) by FIX94\n", arch);

	_tprintf("Please enter the Wii IP:\n");
	char line[30];
	fgets(line, 30, stdin);
	_tprintf("Trying to connect to server\n");
	memset((char *)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(line);
	serv_addr.sin_port = htons(5414);
	while (1) {
		uint32_t message = htonl(0xDEADBEEF);
		if (sendto(sockfd, (const char*)&message, 4, 0, (struct sockaddr *)&serv_addr, serv_len) == SOCKET_ERROR) {
			_tprintf("ERROR sending to server\n");
			goto Exit;
		}
		uint32_t backmsg = 0;
		if (recvfrom(sockfd, (char*)&backmsg, 4, 0, (struct sockaddr *)&serv_addr, &serv_len) == SOCKET_ERROR) {
			_tprintf("ERROR reading from server\n");
			goto Exit;
		}
		if (ntohl(backmsg) == 0xDEADBEEF)
			break;
	}
	// Wii sends out more than one package to ensure we got the message back
	uint32_t tmp;
	while (recvfrom(sockfd, (char*)&tmp, 4, 0, (struct sockaddr *)&serv_addr, &serv_len) == 4) ;
	_tprintf("Connected\n");

	// Make sure to only have a small UDP buffer to not overflow with old data
	int a = 10; //5 packets buffer MAX
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&a, sizeof(int)) == SOCKET_ERROR) {
		_tprintf("ERROR setting socket opts\n");
		goto Exit;
	}

	UINT DevID = 1;

	// Get the driver attributes (Vendor ID, Product ID, Version Number)
	if (!vJoyEnabled())
	{
		_tprintf("Function vJoyEnabled Failed - make sure that vJoy is installed and enabled\n");
		goto Exit;
	}

	// Get the status of the vJoy device before trying to acquire it
	VjdStat status = GetVJDStatus(DevID);

	// Acquire the vJoy device
	if (!AcquireVJD(DevID))
	{
		_tprintf("Failed to acquire vJoy device number %d.\n", DevID);
		goto Exit;
	}
	else
		_tprintf("Acquired device number %d - OK\n", DevID);

	int ret;
	JOYSTICK_POSITION_V2 iReport;
	PVOID pPositionMessage = (PVOID)(&iReport);
	memset(&iReport, 0, sizeof(iReport));

	// Set destenition vJoy device
	iReport.bDevice = DevID;

	// Set position data to the middle
	iReport.wAxisX = 16384;
	iReport.wAxisY = 16384;
	iReport.wAxisZ = 16384;

	iReport.wAxisXRot = 16384;
	iReport.wAxisYRot = 16384;
	iReport.wAxisZRot = 16384;

	LARGE_INTEGER frequency_;
	QueryPerformanceFrequency(&frequency_);
	LARGE_INTEGER startTime_;
	QueryPerformanceCounter(&startTime_);
	LARGE_INTEGER stopTime_;

	int packets = 0, timeouts = 0;
	while (1)
	{
		QueryPerformanceCounter(&stopTime_);
		if (((double)(stopTime_.QuadPart - startTime_.QuadPart) / (double)frequency_.QuadPart) >= 1.0) {
			QueryPerformanceCounter(&startTime_);
			_tprintf("\rPackets per second:%i, Retries per second:%i            ", packets, timeouts);
			if (timeouts > 250 && packets == 0) {
				_tprintf("\nAssuming server quit\n");
				goto Exit;
			}
			packets = 0;
			timeouts = 0;
		}
		//make sure data is ready, security timeout
		ret = recvfromTimeOutUDP(sockfd, 0, 3000);
		if (ret < 0 || ret > 1) {
			_tprintf("ERROR reading from socket\n");
			goto Exit;
		}
		else if (ret == 0) {
			timeouts++;
			continue; //timed out, retry
		}
		//should be safe to read out now
		ret = recvfrom(sockfd, (char*)&buffer, 2, 0, (struct sockaddr *)&serv_addr, &serv_len);
		if (ret != 2) {
			_tprintf("ERROR reading from socket\n");
			goto Exit;
		}
		packets++;

		// Hand over GBA buttons
		iReport.lButtons = ntohs(buffer);

		if (!UpdateVJD(DevID, pPositionMessage))
		{
			_tprintf("Feeding vJoy device number %d failed - try to enable device then press enter\n", DevID);
			getchar();
			AcquireVJD(DevID);
		}
	}
Exit:
	_tprintf("Press enter to exit\n");
	getchar();
	closesocket(sockfd);
	RelinquishVJD(DevID);
	return 0;
}
