/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <gccore.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <network.h>
#include <fcntl.h>
#define IOS_O_NONBLOCK 0x04

//from my tests 50us seems to be the lowest
//safe si transfer delay in between calls
#define SI_TRANS_DELAY 50

extern u8 gba_mb_gba[];
extern u32 gba_mb_gba_size;

u8 *resbuf,*cmdbuf;
volatile u16 pads = 0;
volatile bool ctrlQuit = 0;
volatile u32 ctrlerr = 0;
volatile u32 ctrlread = 0;
volatile u32 ctrlStatReset = 0;
void ctrlcb(s32 chan, u32 ret)
{
	//requested
	if(ctrlQuit)
		return;

	//reset stats around every second
	if(ctrlStatReset) {
		ctrlStatReset = 0;
		ctrlread = 0;
		ctrlerr = 0;
	}

	if(!ret) { //no error, so safe to update buttons
		pads = (~((resbuf[1]<<8)|resbuf[0]))&0x3FF;
		ctrlread++; //save good reads for stats
	}
	else //save error for stats
		ctrlerr++;

	// get new data around every millisecond
	SI_Transfer(1,cmdbuf,1,resbuf,5,ctrlcb,1000);
}

volatile u32 transval = 0;
void transcb(s32 chan, u32 ret)
{
	transval = 1;
}

volatile u32 resval = 0;
void acb(s32 res, u32 val)
{
	resval = val;
}

unsigned int docrc(u32 crc, u32 val)
{
	int i;
	for(i = 0; i < 0x20; i++)
	{
		if((crc^val)&1)
		{
			crc>>=1;
			crc^=0xa1c1;
		}
		else
			crc>>=1;
		val>>=1;
	}
	return crc;
}

void endproc()
{
	printf("GC Button pressed, exit\n");
	exit(0);
}

unsigned int calckey(unsigned int size)
{
	unsigned int ret = 0;
	size=(size-0x200) >> 3;
	int res1 = (size&0x3F80) << 1;
	res1 |= (size&0x4000) << 2;
	res1 |= (size&0x7F);
	res1 |= 0x380000;
	int res2 = res1;
	res1 = res2 >> 0x10;
	int res3 = res2 >> 8;
	res3 += res1;
	res3 += res2;
	res3 <<= 24;
	res3 |= res2;
	res3 |= 0x80808080;

	if((res3&0x200) == 0)
	{
		ret |= (((res3)&0xFF)^0x4B)<<24;
		ret |= (((res3>>8)&0xFF)^0x61)<<16;
		ret |= (((res3>>16)&0xFF)^0x77)<<8;
		ret |= (((res3>>24)&0xFF)^0x61);
	}
	else
	{
		ret |= (((res3)&0xFF)^0x73)<<24;
		ret |= (((res3>>8)&0xFF)^0x65)<<16;
		ret |= (((res3>>16)&0xFF)^0x64)<<8;
		ret |= (((res3>>24)&0xFF)^0x6F);
	}
	return ret;
}

void doreset()
{
	cmdbuf[0] = 0xFF; //reset
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}

void getstatus()
{
	cmdbuf[0] = 0; //status
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}

u32 recv()
{
	memset(resbuf,0,32);
	cmdbuf[0]=0x14; //read
	transval = 0;
	SI_Transfer(1,cmdbuf,1,resbuf,5,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
	return *(vu32*)resbuf;
}

void send(u32 msg)
{
	cmdbuf[0]=0x15;cmdbuf[1]=(msg>>0)&0xFF;cmdbuf[2]=(msg>>8)&0xFF;
	cmdbuf[3]=(msg>>16)&0xFF;cmdbuf[4]=(msg>>24)&0xFF;
	transval = 0;
	resbuf[0] = 0;
	SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,SI_TRANS_DELAY);
	while(transval == 0) ;
}

vu8 updateStat = 0;
void alarmCb(syswd_t alarm,void *cb_arg)
{
	updateStat = 1;
}

int main(int argc, char *argv[]) 
{
	void *xfb = NULL;
	GXRModeObj *rmode = NULL;
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	int x = 24, y = 32, w, h;
	w = rmode->fbWidth - (32);
	h = rmode->xfbHeight - (48);
	CON_InitEx(rmode, x, y, w, h);
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	PAD_Init();
	cmdbuf = memalign(32,32);
	resbuf = memalign(32,32);
	int i;
	puts("Init Network...");
	net_init();

	u32 ip = net_gethostip();
	if(ip == 0)
	{
		puts("Failed!");
		sleep(3);
		exit(0);
	}
	char ipChar[64];
	sprintf(ipChar,"Wii IP: %i.%i.%i.%i",ip>>24,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF);

	while(1)
	{
		printf("\x1b[2J");
		printf("\x1b[37m");
		printf("GBA Link Cable WiFi Input Server v1.2 by FIX94\n");
		puts(ipChar);
		printf("You can press any GC controller button to quit\n");
		printf("Waiting for GBA in port 2...\n");
		resval = 0;
		ctrlerr = false;

		SI_GetTypeAsync(1,acb);
		while(1)
		{
			if(resval)
			{
				if(resval == 0x80 || resval & 8)
				{
					resval = 0;
					SI_GetTypeAsync(1,acb);
				}
				else if(resval)
					break;
			}
			PAD_ScanPads();
			VIDEO_WaitVSync();
			if(PAD_ButtonsHeld(0))
				endproc();
		}
		if(resval & SI_GBA)
		{
			printf("GBA Found, please wait...\n");
			resbuf[2]=0;
			while(!(resbuf[2]&0x10))
			{
				doreset();
				getstatus();
			}
			//printf("Ready, sending input stub\n");
			unsigned int sendsize = ((gba_mb_gba_size+7)&~7);
			unsigned int ourkey = calckey(sendsize);
			//printf("Our Key: %08x\n", ourkey);
			//get current sessionkey
			u32 sessionkeyraw = recv();
			u32 sessionkey = __builtin_bswap32(sessionkeyraw^0x7365646F);
			//send over our own key
			send(__builtin_bswap32(ourkey));
			unsigned int fcrc = 0x15a0;
			//send over gba header
			for(i = 0; i < 0xC0; i+=4)
			{
				send(__builtin_bswap32(*(vu32*)(gba_mb_gba+i)));
				//if(!(resbuf[0]&0x2)) printf("Possible error %02x\n",resbuf[0]);
			}
			//printf("Header done! Sending ROM...\n");
			for(i = 0xC0; i < sendsize; i+=4)
			{
				u32 enc = ((gba_mb_gba[i+3]<<24)|(gba_mb_gba[i+2]<<16)|(gba_mb_gba[i+1]<<8)|(gba_mb_gba[i]));
				fcrc=docrc(fcrc,enc);
				sessionkey = (sessionkey*0x6177614B)+1;
				enc^=sessionkey;
				enc^=((~(i+(0x20<<20)))+1);
				enc^=0x20796220;
				send(enc);
				//if(!(resbuf[0]&0x2)) printf("Possible error %02x\n",resbuf[0]);
			}
			fcrc |= (sendsize<<16);
			//printf("ROM done! CRC: %08x\n", fcrc);
			//send over CRC
			sessionkey = (sessionkey*0x6177614B)+1;
			fcrc^=sessionkey;
			fcrc^=((~(i+(0x20<<20)))+1);
			fcrc^=0x20796220;
			send(fcrc);
			//get crc back (unused)
			recv();
			//prepare stats
			syswd_t timer;
			SYS_CreateAlarm(&timer);
			struct timespec tInterval;
			//update stats every second
			tInterval.tv_sec = 1;
			tInterval.tv_nsec = 0;
			SYS_SetPeriodicAlarm(timer,&tInterval,&tInterval,alarmCb,0);
			//start read chain
			ctrlQuit = false;
			cmdbuf[0] = 0x14; //read
			transval = 0;
			SI_Transfer(1,cmdbuf,1,resbuf,5,ctrlcb,SI_TRANS_DELAY);
			//set up socket
			s32 sock = net_socket(AF_INET, SOCK_DGRAM, 0);
			struct sockaddr_in server;
			server.sin_family = AF_INET;
			server.sin_port = htons(5414);
			server.sin_addr.s_addr = INADDR_ANY;
			net_bind(sock, (struct sockaddr*)&server, sizeof(server));
			//make it as quick as possible
			int32_t flags = net_fcntl(sock, F_GETFL, 0);
			net_fcntl(sock, F_SETFL, flags | IOS_O_NONBLOCK); 
			puts("Waiting for PC Client to connect");
			//wait for PC to connect
			struct sockaddr from;
			memset(&from, 0, sizeof(struct sockaddr));
			uint32_t length = sizeof(struct sockaddr);
			//simple verification loop
			u32 theRes = 0;
			while(theRes != 0xDEADBEEF) {
				net_recvfrom(sock, &theRes, 4, 0, (struct sockaddr *)&from, &length);
				PAD_ScanPads();
				VIDEO_WaitVSync();
				if(PAD_ButtonsHeld(0))
					endproc();
			}
			//send result back to pc
			for(i = 0; i < 10; i++) {
				net_sendto(sock, &theRes, 4, 0, (struct sockaddr *)&from, length);
				VIDEO_WaitVSync();
			}
			printf("All Done, sending inputs to PC\nCurrent Stats per Second:\n   ");
			//hm
			int loops = 0, secToQuit = 15, sleepDelay = 1400;
			while(1)
			{
				if(updateStat)
				{
					if(ctrlread == 0 && ctrlerr > 700) {
						secToQuit--;
						printf("\33[2K\rPackets:%i (%ius sleep in between), GBA lost, quitting in %i seconds",loops,sleepDelay,secToQuit);
					}
					else if(ctrlread < 1000) {
						printf("\33[2K\rPackets:%i (%ius sleep in between), GBA Reads:%i (%i failed)",loops,sleepDelay,ctrlread,ctrlerr);
						secToQuit = 15;
					}
					updateStat = 0;
					ctrlStatReset = 1;
					//anti packet-flood
					if(loops >= 290 && sleepDelay < 1800)
						sleepDelay += 20;
					else if(loops < 270 && sleepDelay > 1000)
						sleepDelay -= 10;
					loops = 0;
				}
				if(secToQuit <= 0) break;
				vu16 data = pads;
				net_sendto(sock, (void*)&data, 2, 0, (struct sockaddr *)&from, length);
				usleep(sleepDelay);
				loops++;
			}
			printf("\33[2K\rGBA disconnected, loop ended");
			SYS_CancelAlarm(timer);
			net_close(sock);
			ctrlQuit = true;
			sleep(2);
			SYS_RemoveAlarm(timer);
		}
	}
	return 0;
}
