/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

 * Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

 * Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

 * Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 05/07/2011
 *****************************************************************************/

#ifndef WIN32
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef LEGACY_WIN32
#include <wspiapi.h>
#endif
#endif
#include <cmath>
#include <sstream>
#include <iostream>
#include "queue.h"
#include "core.h"

using namespace std;


CUDTUnited CUDT::s_UDTUnited;

const UDTSOCKET CUDT::INVALID_SOCK = -1;
const int CUDT::ERROR = -1;

const UDTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int UDT::ERROR = CUDT::ERROR;

const int32_t CSeqNo::m_iSeqNoTH = 0x3FFFFFFF;
const int32_t CSeqNo::m_iMaxSeqNo = 0x7FFFFFFF;
const int32_t CAckNo::m_iMaxAckSeqNo = 0x7FFFFFFF;
const int32_t CMsgNo::m_iMsgNoTH = 0xFFFFFFF;
const int32_t CMsgNo::m_iMaxMsgNo = 0x1FFFFFFF;

const int CUDT::m_iVersion = 4;
const int CUDT::m_iSYNInterval = 1000000;
const int CUDT::m_iSelfClockInterval = 64;


CUDT::CUDT()
{
	m_pSndBuffer = NULL;
	m_pRcvBuffer = NULL;
	m_pSndLossList = NULL;
	m_pRcvLossList = NULL;
	m_pACKWindow = NULL;
	m_pSndTimeWindow = NULL;
	m_pRcvTimeWindow = NULL;

	m_pSndQueue = NULL;
	m_pRcvQueue = NULL;
	m_pPeerAddr = NULL;
	m_pSNode = NULL;
	m_pRNode = NULL;

	// Initilize mutex and condition variables
	initSynch();


	// Default UDT configurations
	m_iMSS = 1500;
	m_bSynSending = true;
	m_bSynRecving = true;
	m_iFlightFlagSize = 100000;
	m_iSndBufSize =100000;
	m_iRcvBufSize = 100000; //Rcv buffer MUST NOT be bigger than Flight Flag size
	m_Linger.l_onoff = 1;
	m_Linger.l_linger = 180;
	m_iUDPSndBufSize = 100000;
	m_iUDPRcvBufSize = m_iRcvBufSize * m_iMSS;
	m_iSockType = UDT_STREAM;
	m_iIPversion = AF_INET;
	m_bRendezvous = false;
	m_iSndTimeOut = -1;
	m_iRcvTimeOut = -1;
	m_bReuseAddr = true;
	lossptr=0;
	m_llMaxBW = -1;

	m_pCCFactory = new CCCFactory<CUDTCC>;
	m_pCC = NULL;
	m_pCache = NULL;

	// Initial status
	m_bOpened = false;
	m_bListening = false;
	m_bConnecting = false;
	m_bConnected = false;
	m_bClosing = false;
	m_bShutdown = false;
	m_bBroken = false;
	m_bPeerHealth = true;
	m_ullLingerExpiration = 0;
	start_ = time(NULL);
	remove( "/home/yossi/timeout_times.txt" );
	for (int i = 0; i < 100; i++) state[i] = 0;
}

CUDT::CUDT(const CUDT& ancestor)
{
	m_pSndBuffer = NULL;
	m_pRcvBuffer = NULL;
	m_pSndLossList = NULL;
	m_pRcvLossList = NULL;
	m_pACKWindow = NULL;
	m_pSndTimeWindow = NULL;
	m_pRcvTimeWindow = NULL;

	m_pSndQueue = NULL;
	m_pRcvQueue = NULL;
	m_pPeerAddr = NULL;
	m_pSNode = NULL;
	m_pRNode = NULL;

	// Initilize mutex and condition variables
	initSynch();

	// Default UDT configurations
	m_iMSS = ancestor.m_iMSS;
	m_bSynSending = ancestor.m_bSynSending;
	m_bSynRecving = ancestor.m_bSynRecving;
	m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
	m_iSndBufSize = ancestor.m_iSndBufSize;
	m_iRcvBufSize = ancestor.m_iRcvBufSize;
	m_Linger = ancestor.m_Linger;
	m_iUDPSndBufSize = ancestor.m_iUDPSndBufSize;
	m_iUDPRcvBufSize = ancestor.m_iUDPRcvBufSize;
	m_iSockType = ancestor.m_iSockType;
	m_iIPversion = ancestor.m_iIPversion;
	m_bRendezvous = ancestor.m_bRendezvous;
	m_iSndTimeOut = ancestor.m_iSndTimeOut;
	m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
	m_bReuseAddr = true;	// this must be true, because all accepted sockets shared the same port with the listener
	m_llMaxBW = ancestor.m_llMaxBW;

	m_pCCFactory = ancestor.m_pCCFactory->clone();
	m_pCC = NULL;
	m_pCache = ancestor.m_pCache;

	// Initial status
	m_bOpened = false;
	m_bListening = false;
	m_bConnecting = false;
	m_bConnected = false;
	m_bClosing = false;
	m_bShutdown = false;
	m_bBroken = false;
	m_bPeerHealth = true;
	m_ullLingerExpiration = 0;
	start_ = time(NULL);
	remove( "/home/yossi/timeout_times.txt" );
	for (int i = 0; i < 100; i++) state[i] = 0;
}

CUDT::~CUDT()
{
	// release mutex/condtion variables
	destroySynch();

	// destroy the data structures
	delete m_pSndBuffer;
	delete m_pRcvBuffer;
	delete m_pSndLossList;
	delete m_pRcvLossList;
	delete m_pACKWindow;
	delete m_pSndTimeWindow;
	delete m_pRcvTimeWindow;
	delete m_pCCFactory;
	delete m_pCC;
	delete m_pPeerAddr;
	delete m_pSNode;
	delete m_pRNode;
}

void CUDT::setOpt(UDTOpt optName, const void* optval, const int&)
{
	if (m_bBroken || m_bClosing)
		throw CUDTException(2, 1, 0);

	CGuard cg(m_ConnectionLock);
	CGuard sendguard(m_SendLock);
	CGuard recvguard(m_RecvLock);

	switch (optName)
	{
	case UDT_MSS:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);

		if (*(int*)optval < int(28 + CHandShake::m_iContentSize))
			throw CUDTException(5, 3, 0);

		m_iMSS = *(int*)optval;

		// Packet size cannot be greater than UDP buffer size
		if (m_iMSS > m_iUDPSndBufSize)
			m_iMSS = m_iUDPSndBufSize;
		if (m_iMSS > m_iUDPRcvBufSize)
			m_iMSS = m_iUDPRcvBufSize;

		break;

	case UDT_SNDSYN:
		m_bSynSending = *(bool *)optval;
		break;

	case UDT_RCVSYN:
		m_bSynRecving = *(bool *)optval;
		break;

	case UDT_CC:
		if (m_bConnecting || m_bConnected)
			throw CUDTException(5, 1, 0);
		if (NULL != m_pCCFactory)
			delete m_pCCFactory;
		m_pCCFactory = ((CCCVirtualFactory *)optval)->clone();

		break;

	case UDT_FC:
		if (m_bConnecting || m_bConnected)
			throw CUDTException(5, 2, 0);

		if (*(int*)optval < 1)
			throw CUDTException(5, 3);

		// Mimimum recv flight flag size is 32 packets
		if (*(int*)optval > 32)
			m_iFlightFlagSize = *(int*)optval;
		else
			m_iFlightFlagSize = 32;

		break;

	case UDT_SNDBUF:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);

		if (*(int*)optval <= 0)
			throw CUDTException(5, 3, 0);

		m_iSndBufSize = *(int*)optval / (m_iMSS - 28);

		break;

	case UDT_RCVBUF:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);

		if (*(int*)optval <= 0)
			throw CUDTException(5, 3, 0);

		// Mimimum recv buffer size is 32 packets
		if (*(int*)optval > (m_iMSS - 28) * 32)
			m_iRcvBufSize = *(int*)optval / (m_iMSS - 28);
		else
			m_iRcvBufSize = 32;

		// recv buffer MUST not be greater than FC size
		if (m_iRcvBufSize > m_iFlightFlagSize)
			m_iRcvBufSize = m_iFlightFlagSize;

		break;

	case UDT_LINGER:
		m_Linger = *(linger*)optval;
		break;

	case UDP_SNDBUF:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);

		m_iUDPSndBufSize = *(int*)optval;

		if (m_iUDPSndBufSize < m_iMSS)
			m_iUDPSndBufSize = m_iMSS;

		break;

	case UDP_RCVBUF:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);

		m_iUDPRcvBufSize = *(int*)optval;

		if (m_iUDPRcvBufSize < m_iMSS)
			m_iUDPRcvBufSize = m_iMSS;

		break;

	case UDT_RENDEZVOUS:
		if (m_bConnecting || m_bConnected)
			throw CUDTException(5, 1, 0);
		m_bRendezvous = *(bool *)optval;
		break;

	case UDT_SNDTIMEO:
		m_iSndTimeOut = *(int*)optval;
		break;

	case UDT_RCVTIMEO:
		m_iRcvTimeOut = *(int*)optval;
		break;

	case UDT_REUSEADDR:
		if (m_bOpened)
			throw CUDTException(5, 1, 0);
		m_bReuseAddr = *(bool*)optval;
		break;

	case UDT_MAXBW:
		if (m_bConnecting || m_bConnected)
			throw CUDTException(5, 1, 0);
		m_llMaxBW = *(int64_t*)optval;
		break;

	default:
		throw CUDTException(5, 0, 0);
	}
}

void CUDT::getOpt(UDTOpt optName, void* optval, int& optlen)
{
	CGuard cg(m_ConnectionLock);

	switch (optName)
	{
	case UDT_MSS:
		*(int*)optval = m_iMSS;
		optlen = sizeof(int);
		break;

	case UDT_SNDSYN:
		*(bool*)optval = m_bSynSending;
		optlen = sizeof(bool);
		break;

	case UDT_RCVSYN:
		*(bool*)optval = m_bSynRecving;
		optlen = sizeof(bool);
		break;

	case UDT_CC:
		if (!m_bOpened)
			throw CUDTException(5, 5, 0);
		*(CCC**)optval = m_pCC;
		optlen = sizeof(CCC*);

		break;

	case UDT_FC:
		*(int*)optval = m_iFlightFlagSize;
		optlen = sizeof(int);
		break;

	case UDT_SNDBUF:
		*(int*)optval = m_iSndBufSize * (m_iMSS - 28);
		optlen = sizeof(int);
		break;

	case UDT_RCVBUF:
		*(int*)optval = m_iRcvBufSize * (m_iMSS - 28);
		optlen = sizeof(int);
		break;

	case UDT_LINGER:
		if (optlen < (int)(sizeof(linger)))
			throw CUDTException(5, 3, 0);

		*(linger*)optval = m_Linger;
		optlen = sizeof(linger);
		break;

	case UDP_SNDBUF:
		*(int*)optval = m_iUDPSndBufSize;
		optlen = sizeof(int);
		break;

	case UDP_RCVBUF:
		*(int*)optval = m_iUDPRcvBufSize;
		optlen = sizeof(int);
		break;

	case UDT_RENDEZVOUS:
		*(bool *)optval = m_bRendezvous;
		optlen = sizeof(bool);
		break;

	case UDT_SNDTIMEO:
		*(int*)optval = m_iSndTimeOut;
		optlen = sizeof(int);
		break;

	case UDT_RCVTIMEO:
		*(int*)optval = m_iRcvTimeOut;
		optlen = sizeof(int);
		break;

	case UDT_REUSEADDR:
		*(bool *)optval = m_bReuseAddr;
		optlen = sizeof(bool);
		break;

	case UDT_MAXBW:
		*(int64_t*)optval = m_llMaxBW;
		optlen = sizeof(int64_t);
		break;

	case UDT_STATE:
		*(int32_t*)optval = s_UDTUnited.getStatus(m_SocketID);
		optlen = sizeof(int32_t);
		break;

	case UDT_EVENT:
	{
		int32_t event = 0;
		if (m_bBroken)
			event |= UDT_EPOLL_ERR;
		else
		{
			if (m_pRcvBuffer && (m_pRcvBuffer->getRcvDataSize() > 0))
				event |= UDT_EPOLL_IN;
			if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
				event |= UDT_EPOLL_OUT;
		}
		*(int32_t*)optval = event;
		optlen = sizeof(int32_t);
		break;
	}

	case UDT_SNDDATA:
		if (m_pSndBuffer)
			*(int32_t*)optval = m_pSndBuffer->getCurrBufSize();
		else
			*(int32_t*)optval = 0;
		optlen = sizeof(int32_t);
		break;

	case UDT_RCVDATA:
		if (m_pRcvBuffer)
			*(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
		else
			*(int32_t*)optval = 0;
		optlen = sizeof(int32_t);
		break;

	default:
		throw CUDTException(5, 0, 0);
	}
}

void CUDT::open()
{
	CGuard cg(m_ConnectionLock);

	// Initial sequence number, loss, acknowledgement, etc.
	m_iPktSize = m_iMSS - 28;
	m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
	m_iRcvPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

	m_iEXPCount = 1;
	m_iBandwidth = 1;
	m_iDeliveryRate = 16;
	m_iAckSeqNo = 0;
	m_ullLastAckTime = 0;

	// trace information
	m_StartTime = CTimer::getTime();
	TotalBytes = m_llSentTotal = m_llRecvTotal = m_iSndLossTotal = m_iRcvLossTotal = m_iRetransTotal = m_iSentACKTotal = m_iRecvACKTotal = m_iSentNAKTotal = m_iRecvNAKTotal = 0;
	m_LastSampleTime = CTimer::getTime();
	m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
	m_llSndDuration = m_llSndDurationTotal = 0;

	// structures for queue
	if (NULL == m_pSNode)
		m_pSNode = new CSNode;
	m_pSNode->m_pUDT = this;
	m_pSNode->m_llTimeStamp = 1;
	m_pSNode->m_iHeapLoc = -1;

	if (NULL == m_pRNode)
		m_pRNode = new CRNode;
	m_pRNode->m_pUDT = this;
	m_pRNode->m_llTimeStamp = 1;
	m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
	m_pRNode->m_bOnList = false;

	m_iRTT = 10 * m_iSYNInterval;
	last_rtt_ = 10 * m_iSYNInterval;
	//for (int i = 0; i < 100; i++) m_last_rtt[i] = 5 * m_iSYNInterval;
	m_monitor_count = 0;
	m_iRTTVar = m_iRTT >> 1;
	m_ullCPUFrequency = CTimer::getCPUFrequency();

	// set up the imers
	m_ullSYNInt = m_iSYNInterval * m_ullCPUFrequency;

	// set minimum NAK and EXP timeout to 100ms
	m_ullMinNakInt = 410000 * m_ullCPUFrequency;
	m_ullMinExpInt = 410000 * m_ullCPUFrequency;

	m_ullACKInt = m_ullSYNInt;
	m_ullNAKInt = m_ullMinNakInt;

	uint64_t currtime;
	CTimer::rdtsc(currtime);
	m_ullLastRspTime = currtime;
	m_ullNextACKTime = currtime + m_ullSYNInt;
	m_ullNextNAKTime = currtime + m_ullNAKInt;

	m_iPktCount = 0;
	m_iLightACKCount = 1;

	m_ullTargetTime = 0;
	m_ullTimeDiff = 0;

	// Now UDT is opened.
	m_bOpened = true;
}

void CUDT::listen()
{
	CGuard cg(m_ConnectionLock);

	if (!m_bOpened)
		throw CUDTException(5, 0, 0);

	if (m_bConnecting || m_bConnected)
		throw CUDTException(5, 2, 0);

	// listen can be called more than once
	if (m_bListening)
		return;

	// if there is already another socket listening on the same port
	if (m_pRcvQueue->setListener(this) < 0)
		throw CUDTException(5, 11, 0);

	m_bListening = true;
}

void CUDT::connect(const sockaddr* serv_addr)
{
	//cout<<m_iFlowWindowSize<<"DING"<<endl;
	//	cout<<"this is the first connection\n";
	CGuard cg(m_ConnectionLock);

	if (!m_bOpened)
		throw CUDTException(5, 0, 0);

	if (m_bListening)
		throw CUDTException(5, 2, 0);

	if (m_bConnecting || m_bConnected)
		throw CUDTException(5, 2, 0);

	// record peer/server address
	delete m_pPeerAddr;
	m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
	memcpy(m_pPeerAddr, serv_addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

	// register this socket in the rendezvous queue
	// RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this function
	uint64_t ttl = 3000000;
	if (m_bRendezvous)
		ttl *= 10;
	ttl += CTimer::getTime();
	m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

	// This is my current configurations
	m_ConnReq.m_iVersion = m_iVersion;
	m_ConnReq.m_iType = m_iSockType;
	m_ConnReq.m_iMSS = m_iMSS;
	m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;
	m_ConnReq.m_iReqType = (!m_bRendezvous) ? 1 : 0;
	m_ConnReq.m_iID = m_SocketID;
	CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion);

	// Random Initial Sequence Number
	srand((unsigned int)CTimer::getTime());
	m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));
	//cout<<"Initial"<<m_iISN<<endl;
	m_iLastDecSeq = m_iISN - 1;
	m_iSndLastAck = m_iISN;
	m_iSndLastDataAck = m_iISN;
	m_iSndCurrSeqNo = m_iISN - 1;
	m_iSndLastAck2 = m_iISN;
	m_iMonitorCurrSeqNo=0;
	m_ullSndLastAck2Time = CTimer::getTime();

	// Inform the server my configurations.
	CPacket request;
	char* reqdata = new char [m_iPayloadSize];
	request.pack(0, NULL, reqdata, m_iPayloadSize);
	// ID = 0, connection request
	request.m_iID = 0;

	int hs_size = m_iPayloadSize;
	m_ConnReq.serialize(reqdata, hs_size);
	request.setLength(hs_size);
	m_pSndQueue->sendto(serv_addr, request);
	m_llLastReqTime = CTimer::getTime();

	m_bConnecting = true;

	// asynchronous connect, return immediately
	if (!m_bSynRecving)
	{
		delete [] reqdata;
		return;
	}

	// Wait for the negotiated configurations from the peer side.
	CPacket response;
	char* resdata = new char [m_iPayloadSize];
	response.pack(0, NULL, resdata, m_iPayloadSize);

	CUDTException e(0, 0);

	while (!m_bClosing)
	{
		// avoid sending too many requests, at most 1 request per 250ms
		if (CTimer::getTime() - m_llLastReqTime > 250000)
		{
			m_ConnReq.serialize(reqdata, hs_size);
			request.setLength(hs_size);
			if (m_bRendezvous)
				request.m_iID = m_ConnRes.m_iID;
			m_pSndQueue->sendto(serv_addr, request);
			m_llLastReqTime = CTimer::getTime();
		}

		response.setLength(m_iPayloadSize);
		if (m_pRcvQueue->recvfrom(m_SocketID, response) > 0)
		{
			if (connect(response) <= 0)
				break;

			// new request/response should be sent out immediately on receving a response
			m_llLastReqTime = 0;
		}

		if (CTimer::getTime() > ttl)
		{
			// timeout
			e = CUDTException(1, 1, 0);
			break;
		}
	}

	delete [] reqdata;
	delete [] resdata;

	if (e.getErrorCode() == 0)
	{
		if (m_bClosing)                                                 // if the socket is closed before connection...
			e = CUDTException(1);
		else if (1002 == m_ConnRes.m_iReqType)                          // connection request rejected
			e = CUDTException(1, 2, 0);
		else if ((!m_bRendezvous) && (m_iISN != m_ConnRes.m_iISN))      // secuity check
			e = CUDTException(1, 4, 0);
	}

	if (e.getErrorCode() != 0)
		throw e;
}

int CUDT::connect(const CPacket& response) throw ()
				{
	// this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
	// returning -1 means there is an error.
	// returning 1 or 2 means the connection is in process and needs more handshake
	//cout<<"this is the 2nd connection\n";
	if (!m_bConnecting)
		return -1;

	if (m_bRendezvous && ((0 == response.getFlag()) || (1 == response.getType())) && (0 != m_ConnRes.m_iType))
	{
		//a data packet or a keep-alive packet comes, which means the peer side is already connected
		// in this situation, the previously recorded response will be used
		goto POST_CONNECT;
	}

	if ((1 != response.getFlag()) || (0 != response.getType()))
		return -1;

	m_ConnRes.deserialize(response.m_pcData, response.getLength());

	if (m_bRendezvous)
	{
		// regular connect should NOT communicate with rendezvous connect
		// rendezvous connect require 3-way handshake
		if (1 == m_ConnRes.m_iReqType)
			return -1;

		if ((0 == m_ConnReq.m_iReqType) || (0 == m_ConnRes.m_iReqType))
		{
			m_ConnReq.m_iReqType = -1;
			// the request time must be updated so that the next handshake can be sent out immediately.
			m_llLastReqTime = 0;
			return 1;
		}
	}
	else
	{
		// set cookie
		if (1 == m_ConnRes.m_iReqType)
		{
			m_ConnReq.m_iReqType = -1;
			m_ConnReq.m_iCookie = m_ConnRes.m_iCookie;
			m_llLastReqTime = 0;
			return 1;
		}
	}

	POST_CONNECT:
	// Remove from rendezvous queue
	m_pRcvQueue->removeConnector(m_SocketID);

	// Re-configure according to the negotiated values.
	m_iMSS = m_ConnRes.m_iMSS;
	m_iFlowWindowSize = m_ConnRes.m_iFlightFlagSize;
	m_iPktSize = m_iMSS - 28;
	m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
	m_iRcvPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
	m_iPeerISN = m_ConnRes.m_iISN;
	m_iRcvLastAck = m_ConnRes.m_iISN;
	m_iRcvLastAckAck = m_ConnRes.m_iISN;
	m_iRcvCurrSeqNo = m_ConnRes.m_iISN - 1;
	m_PeerID = m_ConnRes.m_iID;
	memcpy(m_piSelfIP, m_ConnRes.m_piPeerIP, 16);

	// Prepare all data structures
	try
	{
		m_pSndBuffer = new CSndBuffer(64, m_iPayloadSize);
		m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
		// after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
		m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
		m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
		m_pACKWindow = new CACKWindow(1024);
		m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
		m_pSndTimeWindow = new CPktTimeWindow();
	}
	catch (...)
	{
		throw CUDTException(3, 2, 0);
	}

	CInfoBlock ib;
	ib.m_iIPversion = m_iIPversion;
	CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
	if (m_pCache->lookup(&ib) >= 0)
	{
		m_iRTT = ib.m_iRTT;
		m_iBandwidth = ib.m_iBandwidth;
	}

	m_pCC = m_pCCFactory->create();
	m_pCC->m_UDT = m_SocketID;
	m_pCC->setMSS(m_iMSS);
	m_pCC->setMaxCWndSize((int&)m_iFlowWindowSize);
	m_pCC->setSndCurrSeqNo((int32_t&)m_iSndCurrSeqNo);
	m_pCC->setRcvRate(m_iDeliveryRate);
	m_pCC->setRTT(m_iRTT);
	m_pCC->setBandwidth(m_iBandwidth);
	if (m_llMaxBW > 0)
		m_pCC->setUserParam((char*)&(m_llMaxBW), 8);
	m_pCC->init();

	m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
	m_dCongestionWindow = m_pCC->m_dCWndSize;

	// And, I am connected too.
	m_bConnecting = false;
	m_bConnected = true;

	// start the first monitor
	//   for (int i=0;i<20000;i++)
	//   	retransmission_list[i*3+0] = 0;
	//   max_retransmission_list = 0;
	//   min_retransmission_list_seqNo = -1;
	current_monitor = 0;
	previous_monitor = 0;
	left_monitor = 0;
	monitor_ttl = 0;
//	start_monitor(500000);


	// register this socket for receiving data packets
	m_pRNode->m_bOnList = true;
	m_pRcvQueue->setNewEntry(this);

	// acknowledde any waiting epolls to write
	s_UDTUnited.m_EPoll.enable_write(m_SocketID, m_sPollID);

	// acknowledge the management module.
	s_UDTUnited.connect_complete(m_SocketID);
	//cout<<"finishied connection\n";

	return 0;
				}

void CUDT::connect(const sockaddr* peer, CHandShake* hs)
{
	CGuard cg(m_ConnectionLock);

	// Uses the smaller MSS between the peers
	if (hs->m_iMSS > m_iMSS)
		hs->m_iMSS = m_iMSS;
	else
		m_iMSS = hs->m_iMSS;

	// exchange info for maximum flow window size
	m_iFlowWindowSize = hs->m_iFlightFlagSize;
	//cout<<m_iFlowWindowSize<<"DING"<<endl;
	hs->m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;

	m_iPeerISN = hs->m_iISN;

	m_iRcvLastAck = hs->m_iISN;
	m_iRcvLastAckAck = hs->m_iISN;
	m_iRcvCurrSeqNo = hs->m_iISN - 1;

	m_PeerID = hs->m_iID;
	hs->m_iID = m_SocketID;

	// use peer's ISN and send it back for security check
	m_iISN = hs->m_iISN;

	m_iLastDecSeq = m_iISN - 1;
	m_iSndLastAck = m_iISN;
	m_iSndLastDataAck = m_iISN;
	m_iSndCurrSeqNo = m_iISN - 1;
	m_iSndLastAck2 = m_iISN;
	m_ullSndLastAck2Time = CTimer::getTime();

	// this is a reponse handshake
	hs->m_iReqType = -1;

	// get local IP address and send the peer its IP address (because UDP cannot get local IP address)
	memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
	CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);

	m_iPktSize = m_iMSS - 28;
	m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
	m_iRcvPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

	// Prepare all structures
	try
	{
		m_pSndBuffer = new CSndBuffer(64, m_iPayloadSize);
		m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
		m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
		m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
		m_pACKWindow = new CACKWindow(1024);
		m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
		m_pSndTimeWindow = new CPktTimeWindow();
	}
	catch (...)
	{
		throw CUDTException(3, 2, 0);
	}

	CInfoBlock ib;
	ib.m_iIPversion = m_iIPversion;
	CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
	if (m_pCache->lookup(&ib) >= 0)
	{
		m_iRTT = ib.m_iRTT;
		m_iBandwidth = ib.m_iBandwidth;
	}

	m_pCC = m_pCCFactory->create();
	m_pCC->m_UDT = m_SocketID;
	m_pCC->setMSS(m_iMSS);
	m_pCC->setMaxCWndSize((int&)m_iFlowWindowSize);
	m_pCC->setSndCurrSeqNo((int32_t&)m_iSndCurrSeqNo);
	m_pCC->setRcvRate(m_iDeliveryRate);
	m_pCC->setRTT(m_iRTT);
	m_pCC->setBandwidth(m_iBandwidth);
	if (m_llMaxBW > 0) m_pCC->setUserParam((char*)&(m_llMaxBW), 8);
	m_pCC->init();

	m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
	m_dCongestionWindow = m_pCC->m_dCWndSize;

	m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
	memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

	// And of course, it is connected.
	m_bConnected = true;

	// register this socket for receiving data packets
	m_pRNode->m_bOnList = true;
	m_pRcvQueue->setNewEntry(this);

	//send the response to the peer, see listen() for more discussions about this
	CPacket response;
	int size = CHandShake::m_iContentSize;
	char* buffer = new char[size];
	hs->serialize(buffer, size);
	response.pack(0, NULL, buffer, size);
	response.m_iID = m_PeerID;
	m_pSndQueue->sendto(peer, response);
	delete [] buffer;
}

void CUDT::close()
{
	if (!m_bOpened)
		return;

	if (0 != m_Linger.l_onoff)
	{
		uint64_t entertime = CTimer::getTime();

		while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) && (CTimer::getTime() - entertime < m_Linger.l_linger * 1000000ULL))
		{
			// linger has been checked by previous close() call and has expired
			if (m_ullLingerExpiration >= entertime)
				break;

			if (!m_bSynSending)
			{
				// if this socket enables asynchronous sending, return immediately and let GC to close it later
				if (0 == m_ullLingerExpiration)
					m_ullLingerExpiration = entertime + m_Linger.l_linger * 1000000ULL;

				return;
			}

#ifndef WIN32
			timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 1000000;
			nanosleep(&ts, NULL);
#else
			Sleep(1);
#endif
		}
	}

	// remove this socket from the snd queue
	if (m_bConnected)
		m_pSndQueue->m_pSndUList->remove(this);

	// remove itself from all epoll monitoring
	try
	{
		for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++ i)
			s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
	}
	catch (...)
	{
	}

	if (!m_bOpened)
		return;

	// Inform the threads handler to stop.
	m_bClosing = true;

	CGuard cg(m_ConnectionLock);

	// Signal the sender and recver if they are waiting for data.
	releaseSynch();

	if (m_bListening)
	{
		m_bListening = false;
		m_pRcvQueue->removeListener(this);
	}
	else
	{
		m_pRcvQueue->removeConnector(m_SocketID);
	}

	if (m_bConnected)
	{
		if (!m_bShutdown)
			sendCtrl(5);

		m_pCC->close();

		CInfoBlock ib;
		ib.m_iIPversion = m_iIPversion;
		CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
		ib.m_iRTT = m_iRTT;
		ib.m_iBandwidth = m_iBandwidth;
		m_pCache->update(&ib);

		m_bConnected = false;
	}

	// waiting all send and recv calls to stop
	CGuard sendguard(m_SendLock);
	CGuard recvguard(m_RecvLock);

	// CLOSED.
	m_bOpened = false;
}

int CUDT::send(const char* data, const int& len)
{
	if (UDT_DGRAM == m_iSockType)
		throw CUDTException(5, 10, 0);

	// throw an exception if not connected
	if (m_bBroken || m_bClosing)
		throw CUDTException(2, 1, 0);
	else if (!m_bConnected)
		throw CUDTException(2, 2, 0);

	if (len <= 0)
		return 0;
        //cout<<"before acquire lock"<<endl;
	CGuard sendguard(m_SendLock);
        //cout<<"after send acquire lock"<<endl;

	if (m_pSndBuffer->getCurrBufSize() == 0)
	{
		// delay the EXP timer to avoid mis-fired timeout
		uint64_t currtime;
		CTimer::rdtsc(currtime);
		m_ullLastRspTime = currtime;
	}

	if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
	{
		if (!m_bSynSending)
			throw CUDTException(6, 1, 0);
		else
		{
			// wait here during a blocking sending
#ifndef WIN32
			pthread_mutex_lock(&m_SendBlockLock);
			if (m_iSndTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
					pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
			}
			else
			{
				uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
				timespec locktime;

				locktime.tv_sec = exptime / 1000000;
				locktime.tv_nsec = (exptime % 1000000) * 1000;

				while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
					pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
			}
			pthread_mutex_unlock(&m_SendBlockLock);
#else
			if (m_iSndTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
					WaitForSingleObject(m_SendBlockCond, INFINITE);
			}
			else
			{
				uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

				while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
					WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000));
			}
#endif

			// check the connection status
			if (m_bBroken || m_bClosing)
				throw CUDTException(2, 1, 0);
			else if (!m_bConnected)
				throw CUDTException(2, 2, 0);
			else if (!m_bPeerHealth)
			{
				m_bPeerHealth = true;
				throw CUDTException(7);
			}
		}
	}

	if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
	{
		if (m_iSndTimeOut >= 0)
			throw CUDTException(6, 1, 0);

		return 0;
	}

	int size = (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize;
	if (size > len)
		size = len;

	// record total time used for sending
	if (0 == m_pSndBuffer->getCurrBufSize())
		m_llSndDurationCounter = CTimer::getTime();

	// insert the ueser buffer into the sening list
	//cout<<"adding to buffer"<<endl;
	m_pSndBuffer->addBuffer(data, size);

	// insert this socket to snd list if it is not on the list yet
	//cout<<"update1"<<endl;
	m_pSndQueue->m_pSndUList->update(this, false);

	if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
	{
		// write is not available any more
		s_UDTUnited.m_EPoll.disable_write(m_SocketID, m_sPollID);
	}
     //   cout<<"returning"<<endl;

	return size;
}

int CUDT::recv(char* data, const int& len)
{
	if (UDT_DGRAM == m_iSockType)
		throw CUDTException(5, 10, 0);

	// throw an exception if not connected
	if (!m_bConnected)
		throw CUDTException(2, 2, 0);
	else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
		throw CUDTException(2, 1, 0);

	if (len <= 0)
		return 0;

	CGuard recvguard(m_RecvLock);

	if (0 == m_pRcvBuffer->getRcvDataSize())
	{
		if (!m_bSynRecving)
			throw CUDTException(6, 2, 0);
		else
		{
#ifndef WIN32
			pthread_mutex_lock(&m_RecvDataLock);
			if (m_iRcvTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
					pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
			}
			else
			{
				uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL;
				timespec locktime;

				locktime.tv_sec = exptime / 1000000;
				locktime.tv_nsec = (exptime % 1000000) * 1000;

				while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
				{
					pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime);
					if (CTimer::getTime() >= exptime)
						break;
				}
			}
			pthread_mutex_unlock(&m_RecvDataLock);
#else
			if (m_iRcvTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
					WaitForSingleObject(m_RecvDataCond, INFINITE);
			}
			else
			{
				uint64_t enter_time = CTimer::getTime();

				while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
				{
					int diff = int(CTimer::getTime() - enter_time) / 1000;
					if (diff >= m_iRcvTimeOut)
						break;
					WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut - diff ));
				}
			}
#endif
		}
	}

	// throw an exception if not connected
	if (!m_bConnected)
		throw CUDTException(2, 2, 0);
	else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
		throw CUDTException(2, 1, 0);

	int res = m_pRcvBuffer->readBuffer(data, len);

	if (m_pRcvBuffer->getRcvDataSize() <= 0)
	{
		// read is not available any more
		s_UDTUnited.m_EPoll.disable_read(m_SocketID, m_sPollID);
	}

	if ((res <= 0) && (m_iRcvTimeOut >= 0))
		throw CUDTException(6, 2, 0);

	return res;
}

int CUDT::sendmsg(const char* data, const int& len, const int& msttl, const bool& inorder)
{
	if (UDT_STREAM == m_iSockType)
		throw CUDTException(5, 9, 0);

	// throw an exception if not connected
	if (m_bBroken || m_bClosing)
		throw CUDTException(2, 1, 0);
	else if (!m_bConnected)
		throw CUDTException(2, 2, 0);

	if (len <= 0)
		return 0;

	if (len > m_iSndBufSize * m_iPayloadSize)
		throw CUDTException(5, 12, 0);

	CGuard sendguard(m_SendLock);

	if (m_pSndBuffer->getCurrBufSize() == 0)
	{
		// delay the EXP timer to avoid mis-fired timeout
		uint64_t currtime;
		CTimer::rdtsc(currtime);
		m_ullLastRspTime = currtime;
	}

	if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
	{
		if (!m_bSynSending)
			throw CUDTException(6, 1, 0);
		else
		{
			// wait here during a blocking sending
#ifndef WIN32
			pthread_mutex_lock(&m_SendBlockLock);
			if (m_iSndTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
					pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
			}
			else
			{
				uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
				timespec locktime;

				locktime.tv_sec = exptime / 1000000;
				locktime.tv_nsec = (exptime % 1000000) * 1000;

				while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
					pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
			}
			pthread_mutex_unlock(&m_SendBlockLock);
#else
			if (m_iSndTimeOut < 0)
			{
				while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
					WaitForSingleObject(m_SendBlockCond, INFINITE);
			}
			else
			{
				uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

				while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
					WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000));
			}
#endif

			// check the connection status
			if (m_bBroken || m_bClosing)
				throw CUDTException(2, 1, 0);
			else if (!m_bConnected)
				throw CUDTException(2, 2, 0);
		}
	}

	if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
	{
		if (m_iSndTimeOut >= 0)
			throw CUDTException(6, 1, 0);

		return 0;
	}

	// record total time used for sending
	if (0 == m_pSndBuffer->getCurrBufSize())
		m_llSndDurationCounter = CTimer::getTime();

	// insert the user buffer into the sening list
	m_pSndBuffer->addBuffer(data, len, msttl, inorder);

	// insert this socket to the snd list if it is not on the list yet
	m_pSndQueue->m_pSndUList->update(this, false);

	if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
	{
		// write is not available any more
		s_UDTUnited.m_EPoll.disable_write(m_SocketID, m_sPollID);
	}

	return len;
}

int CUDT::recvmsg(char* data, const int& len)
{
	if (UDT_STREAM == m_iSockType)
		throw CUDTException(5, 9, 0);

	// throw an exception if not connected
	if (!m_bConnected)
		throw CUDTException(2, 2, 0);

	if (len <= 0)
		return 0;

	CGuard recvguard(m_RecvLock);

	if (m_bBroken || m_bClosing)
	{
		int res = m_pRcvBuffer->readMsg(data, len);

		if (m_pRcvBuffer->getRcvMsgNum() <= 0)
		{
			// read is not available any more
			s_UDTUnited.m_EPoll.disable_read(m_SocketID, m_sPollID);
		}

		if (0 == res)
			throw CUDTException(2, 1, 0);
		else
			return res;
	}

	if (!m_bSynRecving)
	{
		int res = m_pRcvBuffer->readMsg(data, len);
		if (0 == res)
			throw CUDTException(6, 2, 0);
		else
			return res;
	}

	int res = 0;
	bool timeout = false;

	do
	{
#ifndef WIN32
		pthread_mutex_lock(&m_RecvDataLock);

		if (m_iRcvTimeOut < 0)
		{
			while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
				pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
		}
		else
		{
			uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL;
			timespec locktime;

			locktime.tv_sec = exptime / 1000000;
			locktime.tv_nsec = (exptime % 1000000) * 1000;

			if (pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime) == ETIMEDOUT)
				timeout = true;

			res = m_pRcvBuffer->readMsg(data, len);
		}
		pthread_mutex_unlock(&m_RecvDataLock);
#else
		if (m_iRcvTimeOut < 0)
		{
			while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
				WaitForSingleObject(m_RecvDataCond, INFINITE);
		}
		else
		{
			if (WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut)) == WAIT_TIMEOUT)
				timeout = true;

			res = m_pRcvBuffer->readMsg(data, len);
		}
#endif

		if (m_bBroken || m_bClosing)
			throw CUDTException(2, 1, 0);
		else if (!m_bConnected)
			throw CUDTException(2, 2, 0);
	} while ((0 == res) && !timeout);

	if (m_pRcvBuffer->getRcvMsgNum() <= 0)
	{
		// read is not available any more
		s_UDTUnited.m_EPoll.disable_read(m_SocketID, m_sPollID);
	}

	if ((res <= 0) && (m_iRcvTimeOut >= 0))
		throw CUDTException(6, 2, 0);

	return res;
}

int64_t CUDT::sendfile(fstream& ifs, int64_t& offset, const int64_t& size, const int& block)
{
	if (UDT_DGRAM == m_iSockType)
		throw CUDTException(5, 10, 0);

	if (m_bBroken || m_bClosing)
		throw CUDTException(2, 1, 0);
	else if (!m_bConnected)
		throw CUDTException(2, 2, 0);

	if (size <= 0)
		return 0;

	CGuard sendguard(m_SendLock);

	if (m_pSndBuffer->getCurrBufSize() == 0)
	{
		// delay the EXP timer to avoid mis-fired timeout
		uint64_t currtime;
		CTimer::rdtsc(currtime);
		m_ullLastRspTime = currtime;
	}

	int64_t tosend = size;
	int unitsize;

	// positioning...
	try
	{
		ifs.seekg((streamoff)offset);
	}
	catch (...)
	{
		throw CUDTException(4, 1);
	}

	// sending block by block
	while (tosend > 0)
	{
		if (ifs.fail())
		{
			//cout<<"error happens during sending"<<endl;
			throw CUDTException(4, 4);
		}

		if (ifs.eof())
			break;

		unitsize = int((tosend >= block) ? block : tosend);

#ifndef WIN32
		pthread_mutex_lock(&m_SendBlockLock);
		while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
			pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
		pthread_mutex_unlock(&m_SendBlockLock);
#else
		while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
			WaitForSingleObject(m_SendBlockCond, INFINITE);
#endif

		if (m_bBroken || m_bClosing)
		{cout<<"ERROR1!"<<endl;throw CUDTException(2, 1, 0);}
		else if (!m_bConnected)
		{cout<<"ERROR2!"<<endl; throw CUDTException(2, 2, 0);}
		else if (!m_bPeerHealth)
		{  cout<<"ERROR3!"<<endl;
		// reset peer health status, once this error returns, the app should handle the situation at the peer side
		m_bPeerHealth = true;
		throw CUDTException(7);
		}

		// record total time used for sending
		if (0 == m_pSndBuffer->getCurrBufSize())
			m_llSndDurationCounter = CTimer::getTime();

		int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

		if (sentsize > 0)
		{
			tosend -= sentsize;
			offset += sentsize;
		}
                //cout<<tosend<<endl;
		// insert this socket to snd list if it is not on the list yet
		m_pSndQueue->m_pSndUList->update(this, false);

		//cout<<"sendfileupdate"<<endl;
	}
	//cout<<"last seq"<<m_iSndCurrSeqNo<<endl;
	if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()-2000)
	{   cout<<"Write disabled"<<endl;
	// write is not available any more
	s_UDTUnited.m_EPoll.disable_write(m_SocketID, m_sPollID);
	}

	return size - tosend;
}

int64_t CUDT::recvfile(fstream& ofs, int64_t& offset, const int64_t& size, const int& block)
{
	if (UDT_DGRAM == m_iSockType)
		throw CUDTException(5, 10, 0);

	if (!m_bConnected)
		throw CUDTException(2, 2, 0);
	else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
		throw CUDTException(2, 1, 0);

	if (size <= 0)
		return 0;

	CGuard recvguard(m_RecvLock);

	int64_t torecv = size;
	int unitsize = block;
	int recvsize;

	// positioning...
	try
	{
		ofs.seekp((streamoff)offset);
	}
	catch (...)
	{
		throw CUDTException(4, 3);
	}

	// receiving... "recvfile" is always blocking
	while (torecv > 0)
	{
		if (ofs.fail())
		{
			// send the sender a signal so it will not be blocked forever
			int32_t err_code = CUDTException::EFILE;
			sendCtrl(8, &err_code);

			throw CUDTException(4, 4);
		}

#ifndef WIN32
		pthread_mutex_lock(&m_RecvDataLock);
		while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
			pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
		pthread_mutex_unlock(&m_RecvDataLock);
#else
		while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
			WaitForSingleObject(m_RecvDataCond, INFINITE);
#endif

		if (!m_bConnected)
			throw CUDTException(2, 2, 0);
		else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
			throw CUDTException(2, 1, 0);

		unitsize = int((torecv >= block) ? block : torecv);
		recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

		if (recvsize > 0)
		{
			torecv -= recvsize;
			offset += recvsize;
		}
	}

	if (m_pRcvBuffer->getRcvDataSize() <= 0)
	{
		// read is not available any more
		s_UDTUnited.m_EPoll.disable_read(m_SocketID, m_sPollID);
	}

	return size - torecv;
}

void CUDT::sample(CPerfMon* perf, bool clear)
{
	if (!m_bConnected)
		throw CUDTException(2, 2, 0);
	if (m_bBroken || m_bClosing)
		throw CUDTException(2, 1, 0);

	uint64_t currtime = CTimer::getTime();
	perf->msTimeStamp = (currtime - m_StartTime) / 1000;
        perf->pktTotalBytes = TotalBytes;
	perf->pktSent = m_llTraceSent;
	perf->pktRecv = m_llTraceRecv;
	perf->pktSndLoss = m_iTraceSndLoss;
	perf->pktRcvLoss = m_iTraceRcvLoss;
	perf->pktRetrans = m_iTraceRetrans;
	perf->pktSentACK = m_iSentACK;
	perf->pktRecvACK = m_iRecvACK;
	perf->pktSentNAK = m_iSentNAK;
	perf->pktRecvNAK = m_iRecvNAK;
	perf->usSndDuration = m_llSndDuration;

	perf->pktSentTotal = m_llSentTotal;
	perf->pktRecvTotal = m_llRecvTotal;
	perf->pktSndLossTotal = m_iSndLossTotal;
	perf->pktRcvLossTotal = m_iRcvLossTotal;
	perf->pktRetransTotal = m_iRetransTotal;
	perf->pktSentACKTotal = m_iSentACKTotal;
	perf->pktRecvACKTotal = m_iRecvACKTotal;
	perf->pktSentNAKTotal = m_iSentNAKTotal;
	perf->pktRecvNAKTotal = m_iRecvNAKTotal;
	perf->usSndDurationTotal = m_llSndDurationTotal;

	double interval = double(currtime - m_LastSampleTime);

	perf->mbpsSendRate = double(m_llTraceSent) * m_iPayloadSize * 8.0 / interval;
	perf->mbpsRecvRate = double(m_llTraceRecv) * m_iPayloadSize * 8.0 / interval;

	perf->usPktSndPeriod = m_ullInterval / double(m_ullCPUFrequency);
	perf->pktFlowWindow = m_iFlowWindowSize;
	perf->pktCongestionWindow = (int)m_dCongestionWindow;
	perf->pktFlightSize = CSeqNo::seqlen(const_cast<int32_t&>(m_iSndLastAck), CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
	perf->msRTT = m_iRTT/1000.0;
	perf->mbpsBandwidth = m_iBandwidth * m_iPayloadSize * 8.0 / 1000000.0;

#ifndef WIN32
	if (0 == pthread_mutex_trylock(&m_ConnectionLock))
#else
		if (WAIT_OBJECT_0 == WaitForSingleObject(m_ConnectionLock, 0))
#endif
		{
			perf->byteAvailSndBuf = (NULL == m_pSndBuffer) ? 0 : (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iMSS;
			perf->byteAvailRcvBuf = (NULL == m_pRcvBuffer) ? 0 : m_pRcvBuffer->getAvailBufSize() * m_iMSS;

#ifndef WIN32
			pthread_mutex_unlock(&m_ConnectionLock);
#else
			ReleaseMutex(m_ConnectionLock);
#endif
		}
		else
		{
			perf->byteAvailSndBuf = 0;
			perf->byteAvailRcvBuf = 0;
		}

	if (clear)
	{
		m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
		m_llSndDuration = 0;
		m_LastSampleTime = currtime;
	}
}

void CUDT::initSynch()
{
#ifndef WIN32
	pthread_mutex_init(&m_SendBlockLock, NULL);
	pthread_cond_init(&m_SendBlockCond, NULL);
	pthread_mutex_init(&m_RecvDataLock, NULL);
	pthread_cond_init(&m_RecvDataCond, NULL);
	pthread_mutex_init(&m_SendLock, NULL);
	pthread_mutex_init(&m_RecvLock, NULL);
	pthread_mutex_init(&m_AckLock, NULL);
	pthread_mutex_init(&m_ConnectionLock, NULL);
	pthread_mutex_init(&m_LossrecordLock, NULL);
#else
	m_SendBlockLock = CreateMutex(NULL, false, NULL);
	m_SendBlockCond = CreateEvent(NULL, false, false, NULL);
	m_RecvDataLock = CreateMutex(NULL, false, NULL);
	m_RecvDataCond = CreateEvent(NULL, false, false, NULL);
	m_SendLock = CreateMutex(NULL, false, NULL);
	m_RecvLock = CreateMutex(NULL, false, NULL);
	m_AckLock = CreateMutex(NULL, false, NULL);
	m_ConnectionLock = CreateMutex(NULL, false, NULL);
#endif
}

void CUDT::destroySynch()
{
#ifndef WIN32
	pthread_mutex_destroy(&m_SendBlockLock);
	pthread_cond_destroy(&m_SendBlockCond);
	pthread_mutex_destroy(&m_RecvDataLock);
	pthread_cond_destroy(&m_RecvDataCond);
	pthread_mutex_destroy(&m_SendLock);
	pthread_mutex_destroy(&m_RecvLock);
	pthread_mutex_destroy(&m_AckLock);
	pthread_mutex_destroy(&m_ConnectionLock);
	pthread_mutex_destroy(&m_LossrecordLock);
#else
	CloseHandle(m_SendBlockLock);
	CloseHandle(m_SendBlockCond);
	CloseHandle(m_RecvDataLock);
	CloseHandle(m_RecvDataCond);
	CloseHandle(m_SendLock);
	CloseHandle(m_RecvLock);
	CloseHandle(m_AckLock);
	CloseHandle(m_ConnectionLock);
#endif
}

void CUDT::releaseSynch()
{
#ifndef WIN32
	// wake up user calls
	pthread_mutex_lock(&m_SendBlockLock);
	pthread_cond_signal(&m_SendBlockCond);
	pthread_mutex_unlock(&m_SendBlockLock);

	pthread_mutex_lock(&m_SendLock);
	pthread_mutex_unlock(&m_SendLock);

	pthread_mutex_lock(&m_RecvDataLock);
	pthread_cond_signal(&m_RecvDataCond);
	pthread_mutex_unlock(&m_RecvDataLock);

	pthread_mutex_lock(&m_RecvLock);
	pthread_mutex_unlock(&m_RecvLock);
#else
	SetEvent(m_SendBlockCond);
	WaitForSingleObject(m_SendLock, INFINITE);
	ReleaseMutex(m_SendLock);
	SetEvent(m_RecvDataCond);
	WaitForSingleObject(m_RecvLock, INFINITE);
	ReleaseMutex(m_RecvLock);
#endif
}

void CUDT::sendCtrl(const int& pkttype, void* lparam, void* rparam, const int& size)
{
	CPacket ctrlpkt;

	switch (pkttype)
	{
	case 2: //010 - Acknowledgement
	{
		int32_t ack;

		// If there is no loss, the ACK is the current largest sequence number plus 1;
		// Otherwise it is the smallest sequence number in the receiver loss list.
		if (0 == m_pRcvLossList->getLossLength())
			ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
		else
			ack = m_pRcvLossList->getFirstLostSeq();

		if (ack == m_iRcvLastAckAck)
			break;

		// send out a lite ACK
		// to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
		if (4 == size)
		{
			ctrlpkt.pack(pkttype, NULL, &ack, size);
			ctrlpkt.m_iID = m_PeerID;
			m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

			break;
		}

		uint64_t currtime;
		CTimer::rdtsc(currtime);

		// There are new received packets to acknowledge, update related information.
		if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
		{
			int acksize = CSeqNo::seqoff(m_iRcvLastAck, ack);
			m_iRcvLastAck = ack;
			m_pRcvBuffer->ackData(acksize);


			// signal a waiting "recv" call if there is any data available
#ifndef WIN32
			pthread_mutex_lock(&m_RecvDataLock);
			if (m_bSynRecving)
				pthread_cond_signal(&m_RecvDataCond);
			pthread_mutex_unlock(&m_RecvDataLock);
#else
			if (m_bSynRecving)
				SetEvent(m_RecvDataCond);
#endif

			// acknowledge any waiting epolls to read
			s_UDTUnited.m_EPoll.enable_read(m_SocketID, m_sPollID);
		}
		else if (ack == m_iRcvLastAck)
		{
			if ((currtime - m_ullLastAckTime) < ((m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency))
				break;
		}
		else
			break;

		// Send out the ACK only if has not been received by the sender before
		if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
		{
			int32_t data[6];

			m_iAckSeqNo = CAckNo::incack(m_iAckSeqNo);
			data[0] = m_iRcvLastAck;
			data[1] = m_iRTT;
			data[2] = m_iRTTVar;
			data[3] = m_pRcvBuffer->getAvailBufSize();
			// a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
			if (data[3] < 2)
				data[3] = 2;

			if (currtime - m_ullLastAckTime > m_ullSYNInt)
			{
				data[4] = m_pRcvTimeWindow->getPktRcvSpeed();
				data[5] = m_pRcvTimeWindow->getBandwidth();
				ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 24);

				CTimer::rdtsc(m_ullLastAckTime);
			}
			else
			{
				ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 16);
			}

			ctrlpkt.m_iID = m_PeerID;
			m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

			m_pACKWindow->store(m_iAckSeqNo, m_iRcvLastAck);

			++ m_iSentACK;
			++ m_iSentACKTotal;
		}

		break;
	}

	case 6: //110 - Acknowledgement of Acknowledgement
		ctrlpkt.pack(pkttype, lparam);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 3: //011 - Loss Report
	{
		if (NULL != rparam)
		{
			if (1 == size)
			{
				// only 1 loss packet
				ctrlpkt.pack(pkttype, NULL, (int32_t *)rparam + 1, 4);
			}
			else
			{
				// more than 1 loss packets
				ctrlpkt.pack(pkttype, NULL, rparam, 8);
			}

			ctrlpkt.m_iID = m_PeerID;
			m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

			++ m_iSentNAK;
			++ m_iSentNAKTotal;

			int32_t* data = new int32_t[m_iRcvPayloadSize / 4];
			int losslen;
			m_pRcvLossList->getLossArray(data, losslen, m_iRcvPayloadSize / 4);
			//   cout<<"This is loss List"<<endl;
			int i=0;

			for (i=0;i<losslen;i++){

				if(data[i]<0)
					data[i]^= 0x80000000;
				//         cout<<data[i]<<endl;
			}
			delete []data;


		}
		else if (m_pRcvLossList->getLossLength() > 0)
		{
			// this is periodically NAK report; make sure NAK cannot be sent back too often

			// read loss list from the local receiver loss list
			int32_t* data = new int32_t[m_iRcvPayloadSize / 4];
			int losslen;
			m_pRcvLossList->getLossArray(data, losslen, m_iRcvPayloadSize / 4);

			if (0 < losslen)
			{
				ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
				ctrlpkt.m_iID = m_PeerID;
				m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

				++ m_iSentNAK;
				++ m_iSentNAKTotal;
			}

			delete [] data;
		}

		// update next NAK time, which should wait enough time for the retansmission, but not too long
		m_ullNAKInt = (m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency;
		int rcv_speed = m_pRcvTimeWindow->getPktRcvSpeed();
		if (rcv_speed > 0)
			m_ullNAKInt += (m_pRcvLossList->getLossLength() * 1000000ULL / rcv_speed) * m_ullCPUFrequency;
		if (m_ullNAKInt < m_ullMinNakInt)
			m_ullNAKInt = m_ullMinNakInt;

		break;
	}

	case 4: //100 - Congestion Warning
		ctrlpkt.pack(pkttype);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		CTimer::rdtsc(m_ullLastWarningTime);

		break;

	case 1: //001 - Keep-alive
		ctrlpkt.pack(pkttype);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 0: //000 - Handshake
		ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 5: //101 - Shutdown
		ctrlpkt.pack(pkttype);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 7: //111 - Msg drop request
		ctrlpkt.pack(pkttype, lparam, rparam, 8);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 8: //1000 - acknowledge the peer side a special error
		ctrlpkt.pack(pkttype, lparam);
		ctrlpkt.m_iID = m_PeerID;
		m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

		break;

	case 32767: //0x7FFF - Resevered for future use

        if (NULL != rparam)
         {
            ctrlpkt.pack(pkttype, NULL, rparam, size);
            ctrlpkt.m_iID = m_PeerID;
            m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         }
		break;

	default:
		break;
	}
}

void CUDT::add_to_loss_record(int32_t loss1, int32_t loss2){
//TODO: loss record does not have lock, this might cause problem

#ifdef EXPERIMENTAL_FEATURE_CONTINOUS_SEND
	pthread_mutex_lock(&m_LossrecordLock);
	loss_record1.push_back(loss1);
	loss_record2.push_back(loss2);
	pthread_mutex_unlock(&m_LossrecordLock);
#endif
}
void CUDT::processCtrl(CPacket& ctrlpkt)
{
	// Just heard from the peer, reset the expiration count.
	m_iEXPCount = 1;
	uint64_t currtime;
	CTimer::rdtsc(currtime);
	m_ullLastRspTime = currtime;

	//int32_t current_time,
	int32_t SeqNoInMonitor;
	int monitorNo;

	switch (ctrlpkt.getType())
	{
	case 2: //010 - Acknowledgement
	{
		int32_t ack;

		// process a lite ACK
		if (4 == ctrlpkt.getLength())
		{
			ack = *(int32_t *)ctrlpkt.m_pcData;
			if (CSeqNo::seqcmp(ack, const_cast<int32_t&>(m_iSndLastAck)) >= 0)
			{
				m_iFlowWindowSize -= CSeqNo::seqoff(const_cast<int32_t&>(m_iSndLastAck), ack);
				m_iSndLastAck = ack;
				//cout<<"lite ACK"<<m_iFlowWindowSize<<endl;
			}

			break;
		}
		// read ACK seq. no.
		ack = ctrlpkt.getAckSeqNo();

		// send ACK acknowledgement
		// number of ACK2 can be much less than number of ACK
		uint64_t now = CTimer::getTime();
		if ((currtime - m_ullSndLastAck2Time > (uint64_t)m_iSYNInterval))
		{
                        //cout<<"send ACK ACK"<<endl;
			sendCtrl(6, &ack);
			m_iSndLastAck2 = ack;
			m_ullSndLastAck2Time = now;
		}

		// Got data ACK
		ack = *(int32_t *)ctrlpkt.m_pcData;

		// check the validation of the ack
		if (CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
		{
			//this should not happen: attack or bug
			m_bBroken = true;
			m_iBrokenCounter = 0;
			break;
		}

		if (CSeqNo::seqcmp(ack, const_cast<int32_t&>(m_iSndLastAck)) >= 0)
		{
			// Update Flow Window Size, must update before and together with m_iSndLastAck
			m_iFlowWindowSize = *((int32_t *)ctrlpkt.m_pcData + 3);
			//cout<<m_iFlowWindowSize<<endl;
			m_iSndLastAck = ack;
		}

		// protect packet retransmission
		CGuard::enterCS(m_AckLock);

		int offset = CSeqNo::seqoff((int32_t&)m_iSndLastDataAck, ack);
		if (offset <= 0)
		{
			// discard it if it is a repeated ACK
			CGuard::leaveCS(m_AckLock);
			m_pSndQueue->m_pSndUList->update(this, false);
			break;
		}

		// acknowledge the sending buffer
		m_pSndBuffer->ackData(offset);
		//TODO: this is a potential performance loss point
#ifdef EXPERIMENTAL_FEATURE_CONTINOUS_SEND
		pthread_mutex_lock(&m_LossrecordLock);
        itr_loss_record1 = loss_record1.begin();
        itr_loss_record2 = loss_record2.begin();
        vector<int32_t>::iterator itr_mark1, itr_mark2;
        itr_mark1 = loss_record1.begin();
        itr_mark2 = loss_record2.begin();
               // cout<<"newround!"<<endl;
                if(!loss_record1.empty()){
		//TODO this part does not consider the situation when sequence number wrapped
		while(itr_loss_record2 != loss_record2.end()){
                //        cout<<*itr_loss_record2<<endl;

			if(CSeqNo::seqcmp(const_cast<int32_t&>(*itr_loss_record2),const_cast<int32_t&>(ack))<0){
                          itr_mark1 = itr_loss_record1;
                          itr_mark2 = itr_loss_record2;
			}
			itr_loss_record2++;
			itr_loss_record1++;

		}
               // cout<<"get out sometimes"<<endl;
		if(CSeqNo::seqcmp(const_cast<int32_t&>(*loss_record2.begin()),const_cast<int32_t&>(ack)>0)){

		}else{
                        if(itr_mark1 == loss_record1.begin()){
                        loss_record1.erase(loss_record1.begin());
                        loss_record2.erase(loss_record2.begin());
                        }else{
			loss_record1.erase(loss_record1.begin(),itr_mark1);
			loss_record2.erase(loss_record2.begin(),itr_mark2);
                        }
		}}

		pthread_mutex_unlock(&m_LossrecordLock);
#endif
                // record total time used for sending
		m_llSndDuration += currtime - m_llSndDurationCounter;
		m_llSndDurationTotal += currtime - m_llSndDurationCounter;
		m_llSndDurationCounter = currtime;

		// update sending variables
		m_iSndLastDataAck = ack;
		m_pSndLossList->remove(CSeqNo::decseq((int32_t&)m_iSndLastDataAck));

		CGuard::leaveCS(m_AckLock);

#ifndef WIN32
		pthread_mutex_lock(&m_SendBlockLock);
		if (m_bSynSending)
			pthread_cond_signal(&m_SendBlockCond);
		pthread_mutex_unlock(&m_SendBlockLock);
#else
		if (m_bSynSending)
			SetEvent(m_SendBlockCond);
#endif

		// acknowledde any waiting epolls to write
		s_UDTUnited.m_EPoll.enable_write(m_SocketID, m_sPollID);

		// insert this socket to snd list if it is not on the list yet
		//cout<<"ctrlupdate"<<endl;
		m_pSndQueue->m_pSndUList->update(this, false);

		// Update RTT
		//m_iRTT = *((int32_t *)ctrlpkt.m_pcData + 1);
		//m_iRTTVar = *((int32_t *)ctrlpkt.m_pcData + 2);
		//int rtt = *((int32_t *)ctrlpkt.m_pcData + 1);
		//m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
		//m_iRTT = (m_iRTT * 7 + rtt) >> 3;

		m_pCC->setRTT(m_iRTT);

		if (ctrlpkt.getLength() > 16)
		{
			// Update Estimated Bandwidth and packet delivery rate
			if (*((int32_t *)ctrlpkt.m_pcData + 4) > 0)
				m_iDeliveryRate = (m_iDeliveryRate * 7 + *((int32_t *)ctrlpkt.m_pcData + 4)) >> 3;

			if (*((int32_t *)ctrlpkt.m_pcData + 5) > 0)
				m_iBandwidth = (m_iBandwidth * 7 + *((int32_t *)ctrlpkt.m_pcData + 5)) >> 3;

			m_pCC->setRcvRate(m_iDeliveryRate);
			m_pCC->setBandwidth(m_iBandwidth);
		}

		// how about duplicate ack
		//      if (monitor)
		//	      check_monitor_end_ack(ack);



		m_pCC->onACK(ack);
		// update CC parameters
		m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
		m_dCongestionWindow = m_pCC->m_dCWndSize;

		++ m_iRecvACK;
		++ m_iRecvACKTotal;

		break;
	}

	case 6: //110 - Acknowledgement of Acknowledgement
	{
		int32_t ack;
		int rtt = -1;

		// update RTT
		rtt = m_pACKWindow->acknowledge(ctrlpkt.getAckSeqNo(), ack);
		if (rtt <= 0)
			break;

		//if increasing delay detected...
		//   sendCtrl(4);

		// RTT EWMA
		//m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
		//m_iRTT = (m_iRTT * 7 + rtt) >> 3;

		//m_pCC->setRTT(m_iRTT);

		// update last ACK that has been received by the sender
		if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
			m_iRcvLastAckAck = ack;

		break;
	}

	case 3: //011 - Loss Report
	{
		int32_t* losslist = (int32_t *)(ctrlpkt.m_pcData);
		m_pCC->onLoss(losslist, ctrlpkt.getLength() / 4);
		// update CC parameters
		// m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
		m_dCongestionWindow = m_pCC->m_dCWndSize;
                timeval t;
                gettimeofday(&t, 0);
		//cout<<"losshere"<<t.tv_usec<<endl;
		bool secure = true;
		// decode loss list message and insert loss into the sender loss list
		for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++ i)
		{
			if (0 != (losslist[i] & 0x80000000))
			{
				if ((CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, losslist[i + 1]) > 0) || (CSeqNo::seqcmp(losslist[i + 1], const_cast<int32_t&>(m_iSndCurrSeqNo)) > 0))
				{
					// seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
					secure = false;
					break;
				}

				int num = 0;
				//cerr<<(losslist[i]& 0x7FFFFFFF)<<'\t'<<losslist[i+1]<<endl;
				if (CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, const_cast<int32_t&>(m_iSndLastAck)) >= 0)
				{   num = m_pSndLossList->insert(losslist[i] & 0x7FFFFFFF, losslist[i + 1]);
                                add_to_loss_record(losslist[i]& 0x7FFFFFFF, losslist[i+1]);
//				loss_record1[lossptr]=losslist[i]& 0x7FFFFFFF;
//				loss_record2[lossptr]=losslist[i+1];
				//lossptr++;
				//cout<<"L"<<lossptr<<endl;
				}

				else if (CSeqNo::seqcmp(losslist[i + 1], const_cast<int32_t&>(m_iSndLastAck)) >= 0)
				{               num = m_pSndLossList->insert(const_cast<int32_t&>(m_iSndLastAck), losslist[i + 1]);
                                add_to_loss_record(const_cast<int32_t&>(m_iSndLastAck), losslist[i+1]);
//				loss_record1[lossptr]=m_iSndLastAck;
				//loss_record2[lossptr]=losslist[i+1];
				//lossptr++;
				//cout<<"L"<<lossptr<<endl;
				}//cout<<"This is lost packet with seq. No. as "<<losslist[i]<<endl;
				m_iTraceSndLoss += num;
				m_iSndLossTotal += num;

				++ i;
			}
			else if (CSeqNo::seqcmp(losslist[i], const_cast<int32_t&>(m_iSndLastAck)) >= 0)
			{
				if (CSeqNo::seqcmp(losslist[i], const_cast<int32_t&>(m_iSndCurrSeqNo)) > 0)
				{
					//seq_a must not be greater than the most recent sent seq
					secure = false;
					break;
				}

				int num = m_pSndLossList->insert(losslist[i], losslist[i]);
                                add_to_loss_record(losslist[i], losslist[i]);
//				loss_record1[lossptr]=losslist[i];
//				loss_record2[lossptr]=losslist[i];
				//lossptr++;
				//cout<<"L"<<lossptr<<endl;
				m_iTraceSndLoss += num;
				m_iSndLossTotal += num;
			}
		}

		if (!secure)
		{
			//this should not happen: attack or bug
			cout<<"This is insecure!"<<endl;
			m_bBroken = true;
			m_iBrokenCounter = 0;
			break;
		}

		// the lost packet (retransmission) should be sent out immediately
		//cout<<"loss update"<<endl;
		m_pSndQueue->m_pSndUList->update(this, false);

		//++ m_iRecvNAK;
		//++ m_iRecvNAKTotal;
		//   cout<<m_iRecvNAKTotal<<endl;;
		break;
	}

	case 4: //100 - Delay Warning
		// One way packet delay is increasing, so decrease the sending rate
		m_ullInterval = (uint64_t)ceil(m_ullInterval * 1.125);
		m_iLastDecSeq = m_iSndCurrSeqNo;

		break;

	case 1: //001 - Keep-alive
		// The only purpose of keep-alive packet is to tell that the peer is still alive
		// nothing needs to be done.

		break;

	case 0: //000 - Handshake
	{
		CHandShake req;
		req.deserialize(ctrlpkt.m_pcData, ctrlpkt.getLength());
		if ((req.m_iReqType > 0) || (m_bRendezvous && (req.m_iReqType != -2)))
		{
			// The peer side has not received the handshake message, so it keeps querying
			// resend the handshake packet

			CHandShake initdata;
			initdata.m_iISN = m_iISN;
			initdata.m_iMSS = m_iMSS;
			initdata.m_iFlightFlagSize = m_iFlightFlagSize;
			initdata.m_iReqType = (!m_bRendezvous) ? -1 : -2;
			initdata.m_iID = m_SocketID;

			char* hs = new char [m_iRcvPayloadSize];
			int hs_size = m_iRcvPayloadSize;
			initdata.serialize(hs, hs_size);
			sendCtrl(0, NULL, hs, hs_size);
			delete [] hs;
		}

		break;
	}

	case 5: //101 - Shutdown
		m_bShutdown = true;
		m_bClosing = true;
		m_bBroken = true;
		m_iBrokenCounter = 60;

		// Signal the sender and recver if they are waiting for data.
		releaseSynch();

		CTimer::triggerEvent();

		break;

	case 7: //111 - Msg drop request
		m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq());
		m_pRcvLossList->remove(*(int32_t*)ctrlpkt.m_pcData, *(int32_t*)(ctrlpkt.m_pcData + 4));

		// move forward with current recv seq no.
		if ((CSeqNo::seqcmp(*(int32_t*)ctrlpkt.m_pcData, CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
				&& (CSeqNo::seqcmp(*(int32_t*)(ctrlpkt.m_pcData + 4), m_iRcvCurrSeqNo) > 0))
		{
			m_iRcvCurrSeqNo = *(int32_t*)(ctrlpkt.m_pcData + 4);
		}

		break;

	case 8: // 1000 - An error has happened to the peer side
		//int err_type = packet.getAddInfo();

		// currently only this error is signalled from the peer side
		// if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
		// giving the app a chance to fix the issue

		m_bPeerHealth = false;

		break;

	case 32767: //0x7FFF - reserved and user defined messages
	{
		//m_pCC->processCustomMsg(&ctrlpkt);
		int32_t current_time;
		int32_t* tsn_payload = (int32_t *)(ctrlpkt.m_pcData);
		int last_position = (int)(ctrlpkt.getLength() / 4)-1;
		int Mon = tsn_payload[last_position]>>16;
		rtt_count[Mon]++;
		rtt_value[Mon]+= int(CTimer::getTime() - m_StartTime) - send_timestamp[Mon][tsn_payload[last_position]&0xFFFF];
		if(latency_time_start[Mon] == 0){
			latency_time_start[Mon]=ctrlpkt.m_iTimeStamp;
			latency_seq_start[Mon] = tsn_payload[last_position] & 0xFFFF;
		} else{
			latency_time_end[Mon] = ctrlpkt.m_iTimeStamp;
			latency_seq_end[Mon] = tsn_payload[last_position] & 0xFFFF;
		}
		for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++ i) {
	        lock_guard<mutex> lck(monitor_mutex_);
			monitorNo = tsn_payload[i] >> 16;
			SeqNoInMonitor = tsn_payload[i] & 0xFFFF;
                //cout<<"mon is "<<monitorNo<<" "<<SeqNoInMonitor<<endl;
			++ m_iRecvNAKTotal;
			//cout<<monitorNo<<' '<<SeqNoInMonitor<<' '<<current_monitor<<' '<<left_monitor<<endl;
			// recv pkt
			left[monitorNo]++;
			//cout<<monitorNo<<' '<<SeqNoInMonitor<<endl;
			recv_ack[monitorNo][SeqNoInMonitor] = true;
			current_time = CTimer::getTime();
            bool includeThisMonitor = false;
            if (SeqNoInMonitor == total[monitorNo] -1) {
                //cout<<"include this mon"<<endl;
                includeThisMonitor = true;
            }
			//pkt_sending[monitorNo][SeqNoInMonitor] = ctrlpkt.m_iTimeStamp;
			//cout<<pkt_sending[monitorNo][SeqNoInMonitor]<<endl;

			if (left_monitor) {

				// find out the monitor which didn't end

				int tmp;
                if(includeThisMonitor) {
                    tmp = monitorNo;
                } else {
                    tmp = (monitorNo+99)%100;
                }
				int count=0;
				//.....................
				while (tmp!=current_monitor) {
					if (state[tmp]==2) {
						//cerr<<"TEST "<<current_monitor<<" "<<state[tmp]<<endl;
						for(int i=0;i<total[tmp];i++){
							if(recv_ack[tmp][i]){
								//latency[tmp]+=pkt_sending[tmp][i];
								count++;
							}
						}
						if(count>0)
							latency[tmp]/=count;
						//cout<<latency[tmp]<<' '<<(total[tmp]-left[tmp])<<' '<<count<<endl;
						state[tmp] = 3;
						lost[tmp]=total[tmp]-left[tmp];
						end_time[tmp] = current_time;
						left_monitor--;
						//cerr<<"Killed monitor"<<left[tmp]<<endl;
						//cerr<<"latency info"<<" "<<double(latency_time_end[tmp]-latency_time_start[tmp])/left[tmp]/m_pCC->m_dPktSndPeriod<<endl;
						//cerr<<"latency info"<<" "<<double(latency_time_end[tmp]-latency_time_start[tmp])/(end_transmission_time[tmp]-start_time[tmp]);
//						cerr<<"latency info"<<" "<<latency_time_end[tmp]<<" "<<latency_time_start[tmp]<<" "<<m_pCC->m_dPktSndPeriod<<" "<<latency_seq_end[tmp]<<" "<<latency_seq_start[tmp]<<endl;
						if(rtt_count[Mon]==0){
                                                  //cout<<"zero "<<Mon<<endl;
							rtt_value[Mon]=0;
							rtt_count[Mon]=1;
                        }
                                                //c<<"before on monitor ends"<<Mon<<" "<<rtt_value[Mon]<<" "<<rtt_count[Mon]<<endl;
                                                //cout<<"before on monitor "<<tmp<< "ends loss is"<<total[tmp] - left[tmp]<<endl;
                        double latency_info1 = double(latency_time_end[tmp]*1000-(start_time[tmp]-1471053700000000+m_iRTT/2))/(end_transmission_time[tmp]-start_time[tmp]);
                                                m_iRTT = rtt_value[Mon]/double(rtt_count[Mon]);
						last_rtt_ = m_iRTT;
	                                        m_pCC->setRTT(m_iRTT);
                        double latency_info2 = double(latency_time_end[tmp]*1000-(start_time[tmp]-1471053700000000+m_iRTT/2))/(end_transmission_time[tmp]-start_time[tmp]);

                        double latency_info;
                        if (latency_info1 > latency_info2) {
                        latency_info = latency_info1;
                        } else {
                            latency_info = latency_info2;
                        }
                        //cout<<latency_time_end[tmp] - latency_time_start[tmp]<<" "<<end_transmission_time[tmp]-start_time[tmp]<<endl;
                        //cout<<latency_time_end[tmp]*1000<<" "<<start_time[tmp] -1470892000000000<<endl;
						cerr<<"latency info"<<" "<<latency_info<<endl;

						m_last_rtt.push_front(last_rtt_);
						if (m_last_rtt.size() > kRTTHistorySize) {
							m_last_rtt.pop_back();
						}

						//m_last_rtt[Mon % 100] = rtt_value[Mon]/((double) rtt_count[Mon]);
                                                //cout<<"Fill in rtt value as"<<m_last_rtt[Mon % 100]<<endl;
                                                //cerr<<"Monitor"<<tmp<<"ends at"<<CTimer::getTime()<<endl;

						m_pCC->onMonitorEnds(total[tmp],total[tmp]-left[tmp],(end_transmission_time[tmp]-start_time[tmp])/1000000,current_monitor,tmp, rtt_value[Mon]/double(rtt_count[Mon]), latency_info);
						m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
						if (!left_monitor) break;
					}
					tmp = (tmp+99)%100;
				}
			}
		}

		//cout<<()<<' '<<()<<endl;
		//cout<<tmp<<endl;
		// update CC parameters

		//   m_dCongestionWindow = m_pCC->m_dCWndSize;

		break;
	}
	default:
		break;
	}
}

void CUDT::resizeMSS(int mss) {
    //cout<<"entering resize function"<<endl;
	//CGuard sendguard(m_SendLock);
    m_iMSS = mss;
	m_iPktSize = m_iMSS - 28;
	m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
    //cout<<"before resize"<<endl;
    m_pSndBuffer->resizeMSS(m_iPayloadSize);
    //cout<<"after resize"<<endl;
}

int CUDT::packData(CPacket& packet, uint64_t& ts)
{

	int payload = 0;
	bool probe = false;
	uint64_t entertime;
	CTimer::rdtsc(entertime);
	if(timeout_monitors()) {
        // if there is any thing get timeouted, reset monitor
       start_monitor(0);
    }

	if ((0 != m_ullTargetTime) && (entertime > m_ullTargetTime))
		m_ullTimeDiff += entertime - m_ullTargetTime;
	// Loss retransmission always has higher priority.
	if ((packet.m_iSeqNo = m_pSndLossList->getLostSeq()) >= 0)
	{
		// protect m_iSndLastDataAck from updating by ACK processing
		CGuard ackguard(m_AckLock);


		int offset = CSeqNo::seqoff((int32_t&)m_iSndLastDataAck, packet.m_iSeqNo);
		if (offset < 0)
			return 0;
//		cout<<"pack loss"<<offset<<endl;
		int msglen;
		//struct timeval begin,end;
		//gettimeofday(&begin,NULL);
		payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, msglen);
		//gettimeofday(&end,NULL);
		//cerr<<end.tv_usec-begin.tv_usec<<endl;
		//  cout<<packet.m_iMsgNo<<endl;
		packet.m_iMsgNo=m_iMonitorCurrSeqNo | (current_monitor<<16);

		m_iMonitorCurrSeqNo++;
		if (-1 == payload)
		{
			int32_t seqpair[2];
			seqpair[0] = packet.m_iSeqNo;
			seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);
			sendCtrl(7, &packet.m_iMsgNo, seqpair, 8);
			cout<<"DROP!"<<endl;
			// only one msg drop request is necessary
			m_pSndLossList->remove(seqpair[1]);

			// skip all dropped packets
			if (CSeqNo::seqcmp(const_cast<int32_t&>(m_iSndCurrSeqNo), CSeqNo::incseq(seqpair[1])) < 0)
				m_iSndCurrSeqNo = CSeqNo::incseq(seqpair[1]);

			return 0;
		}
		else if (0 == payload)
			return 0;

		// change lost in current monitor
		if (monitor) {
			//cout<<"this is a retransmission for seq. No. "<<packet.m_iSeqNo<<" <"<<current_monitor<<">"<<endl;
			//left[current_monitor]++;
			retransmission[current_monitor]++;
			monitor_ttl--;
			//pkt_sending[current_monitor][m_iMonitorCurrSeqNo] = CTimer::getTime();
			if (test==1){
				//			cout<<"this is a retransmission for seq. No. "<<packet.m_iSeqNo<<endl;
				test=0;
			}
			if (monitor_ttl==0){
				//cerr<<"this monitor has ended"<<current_monitor<<" "<<left[current_monitor]<<endl;
				end_transmission_time[current_monitor] = CTimer::getTime();
				state[current_monitor] = 2;
				left_monitor++;
				start_monitor(100000);
			}
		}

		++ m_iTraceRetrans;
		++ m_iRetransTotal;
	}
	else
	{
		// If no loss, pack a new packet.
		//cerr<<"NE"<<endl;
		// check congestion/flow window limit
		//cout<<m_iFlowWindowSize<<endl;
		int cwnd = (m_iFlowWindowSize < (int)m_dCongestionWindow) ? m_iFlowWindowSize : (int)m_dCongestionWindow;
		if (cwnd -1000 >= CSeqNo::seqlen(const_cast<int32_t&>(m_iSndLastAck), CSeqNo::incseq(m_iSndCurrSeqNo)))
		{
			struct timeval begin, end;
			gettimeofday(&begin, NULL);
			if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo)))
			{
        if (monitor_ttl==0){
                start_monitor(100000);}
				m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
				m_pCC->setSndCurrSeqNo((int32_t&)m_iSndCurrSeqNo);
				packet.m_iMsgNo=m_iMonitorCurrSeqNo | (current_monitor<<16);
                                //cout<<m_iSndCurrSeqNo<<endl;
				m_iMonitorCurrSeqNo++;
				packet.m_iSeqNo = m_iSndCurrSeqNo;
//				cout<<"m_iSndCurrSeqNo"<<m_iSndCurrSeqNo<<endl;
				// every 16 (0xF) packets, a packet pair is sent
				if (0 == (packet.m_iSeqNo & 0xF))
					probe = false;
                                if (monitor_ttl==0){
                                        start_monitor(100000);}

				//            cout<<"this is a new transmission for seq. No. "<<packet.m_iSeqNo<<endl;
				// monitor function
				if (monitor) {
					//left[current_monitor]++;
					new_transmission[current_monitor]++;
					monitor_ttl--;
                                        //cout<<"send!"<<endl;
					//pkt_sending[current_monitor][m_iMonitorCurrSeqNo] = CTimer::getTime();
					if (test==1){
						//			cout<<"this is a new transmission for seq. No. "<<packet.m_iSeqNo<<endl;
						test=0;
					}
					if (monitor_ttl==0){
						//cerr<<"this monitor has ended"<<current_monitor<<" "<<left[current_monitor]<<endl;
						end_transmission_time[current_monitor] = CTimer::getTime();
						state[current_monitor] = 2;
						left_monitor++;
						start_monitor(100000);
					}
				}
			}
			else
			{
#ifndef EXPERIMENTAL_FEATURE_CONTINOUS_SEND
if(m_iSndLastAck<=m_iSndCurrSeqNo)
m_pSndLossList->insert(const_cast<int32_t&>(m_iSndLastAck), const_cast<int32_t&>(m_iSndCurrSeqNo));
#endif

#ifdef EXPERIMENTAL_FEATURE_CONTINOUS_SEND
//first step: if you have some data that definitely lost (last loss report till end)
//second step: add data from the tail of the vector, insert all of them to lost list
//third step: pack a data and send out in this step
//fourth step: do the timing control right, don't let packData wait for a very long time
				//int msglen;
				if(!loss_record1.empty()){
//					pthread_mutex_lock(&m_LossrecordLock);
//					itr_loss_record1 = loss_record1.begin();
//					itr_loss_record2 = loss_record2.begin();
//					while(itr_loss_record1 < loss_record1.end()){
//						m_pSndLossList->insert(*itr_loss_record1, *itr_loss_record2);
//						itr_loss_record1++;
                                               // cout<<"this should not happend1"<<endl;
//						itr_loss_record2++;
//					}


					if(loss_record2.back()+1 < m_iSndCurrSeqNo){
//                                               m_pSndLossList->insert(const_cast<int32_t&>(m_iSndLastAck), const_cast<int32_t&>(m_iSndCurrSeqNo));
						m_pSndLossList->insert(loss_record2.back()+2, const_cast<int32_t&>(m_iSndCurrSeqNo));
                                                int32_t payload[1];
                                                payload[0]=1;
                                                sendCtrl(32767, NULL, payload, 1*4);
//cout<<loss_record2.back()+2<<" "<<m_iSndCurrSeqNo<<endl;
                                               // cout<<"this should not happend2"<<endl;
					}else{
      //                                          m_pSndLossList->insert(*loss_record1.begin(), *loss_record2.begin());
                                        }
					pthread_mutex_unlock(&m_LossrecordLock);


				}
				else{
                                                m_pSndLossList->insert(const_cast<int32_t&>(m_iSndCurrSeqNo), const_cast<int32_t&>(m_iSndCurrSeqNo));
//                                      cout<<"inserting tail2 "<<m_pSndLossList->getLostSeq()<<endl;
                                     }
/*		if (m_ullTimeDiff >= m_ullInterval)
		{
			ts = entertime;
			m_ullTimeDiff -= m_ullInterval;
                        cout<<"Hurry!"<<m_ullTimeDiff<<" "<<m_ullInterval<<endl;
		}
		else
		{
			ts = entertime + m_ullInterval - m_ullTimeDiff;
			m_ullTimeDiff = 0;
		}
*/
                ts = entertime +500* m_ullCPUFrequency; // m_ullInterval;
                m_ullTargetTime = ts;
                return 0;
#else
                ts = entertime +500* m_ullCPUFrequency; // m_ullInterval;
                m_ullTargetTime = ts;
                return 0;

/*				m_ullTargetTime = 0;
				m_ullTimeDiff = 0;
				ts = 0;
				return 0;*/
#endif
			}
			gettimeofday(&end,NULL);
		}
		else
		{

                //cout<<"CongestReach!"<<cwnd<<endl;
                ts = entertime +500* m_ullCPUFrequency; // m_ullInterval;
                m_ullTargetTime = ts;
                return 0;
		}
	}

	packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
    send_timestamp[packet.m_iMsgNo>>16][packet.m_iMsgNo & 0xFFFF] = packet.m_iTimeStamp;
	packet.m_iID = m_PeerID;
	packet.setLength(payload);
	m_pCC->onPktSent(&packet);


	++ m_llSentTotal;
	++ m_llTraceSent;

	if (probe)
	{
		// sends out probing packet pair
		ts = entertime;
		probe = false;
	}
	else
	{
#ifndef NO_BUSY_WAITING
		ts = entertime + m_ullInterval;
#else
		if (m_ullTimeDiff >= m_ullInterval)
		{
			ts = entertime;
			m_ullTimeDiff -= m_ullInterval;
		}
		else
		{
			ts = entertime + m_ullInterval - m_ullTimeDiff;
			m_ullTimeDiff = 0;
		}
#endif
	}

	m_ullTargetTime = ts;
        TotalBytes+=payload;
	return payload;
}

int CUDT::processData(CUnit* unit)
{
	CPacket& packet = unit->m_Packet;

	// Just heard from the peer, reset the expiration count.
	m_iEXPCount = 1;
	uint64_t currtime;
	CTimer::rdtsc(currtime);
	m_ullLastRspTime = currtime;

	m_pCC->onPktReceived(&packet);
	++ m_iPktCount;
	// update time information
	m_pRcvTimeWindow->onPktArrival();

	// check if it is probing packet pair
	if (0 == (packet.m_iSeqNo & 0xF))
		m_pRcvTimeWindow->probe1Arrival();
	else if (1 == (packet.m_iSeqNo & 0xF))
		m_pRcvTimeWindow->probe2Arrival();

	++ m_llTraceRecv;
	++ m_llRecvTotal;

	int32_t offset = CSeqNo::seqoff(m_iRcvLastAck, packet.m_iSeqNo);
	if ((offset < 0) || (offset >= m_pRcvBuffer->getAvailBufSize()))
		return -1;

	if (m_pRcvBuffer->addData(unit, offset) < 0)
		return -1;

	// Loss detection.
	if (CSeqNo::seqcmp(packet.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
	{
		// If loss found, insert them to the receiver loss list
		m_pRcvLossList->insert(CSeqNo::incseq(m_iRcvCurrSeqNo), CSeqNo::decseq(packet.m_iSeqNo));

		// pack loss list for NAK
		int32_t lossdata[2];
		lossdata[0] = CSeqNo::incseq(m_iRcvCurrSeqNo) | 0x80000000;
		lossdata[1] = CSeqNo::decseq(packet.m_iSeqNo);

		// Generate loss report immediately.
		sendCtrl(3, NULL, lossdata, (CSeqNo::incseq(m_iRcvCurrSeqNo) == CSeqNo::decseq(packet.m_iSeqNo)) ? 1 : 2);

		int loss = CSeqNo::seqlen(m_iRcvCurrSeqNo, packet.m_iSeqNo) - 2;
		m_iTraceRcvLoss += loss;
		m_iRcvLossTotal += loss;
	}

	// This is not a regular fixed size packet...
	//an irregular sized packet usually indicates the end of a message, so send an ACK immediately
	if (packet.getLength() != m_iPayloadSize)
		CTimer::rdtsc(m_ullNextACKTime);

	// Update the current largest sequence number that has been received.
	// Or it is a retransmitted packet, remove it from receiver loss list.
	if (CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
		m_iRcvCurrSeqNo = packet.m_iSeqNo;
	else
	{
		int split=0;
		split=m_pRcvLossList->remove(packet.m_iSeqNo);

		if(split>1)
		{      int32_t lossdata[2];
		lossdata[0] = m_pRcvLossList->m_piData1[split-2];
		lossdata[1] = m_pRcvLossList->m_piData2[split-2];
		sendCtrl(3,NULL,lossdata,(m_pRcvLossList->m_piData2[split-2] == -1) ? 1 : 2);

		}

	}
	return 0;
}

int CUDT::listen(sockaddr* addr, CPacket& packet)
{
	if (m_bClosing)
		return 1002;

	if (packet.getLength() != CHandShake::m_iContentSize)
		return 1004;

	CHandShake hs;
	hs.deserialize(packet.m_pcData, packet.getLength());

	// SYN cookie
	char clienthost[NI_MAXHOST];
	char clientport[NI_MAXSERV];
	getnameinfo(addr, (AF_INET == m_iVersion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), clienthost, sizeof(clienthost), clientport, sizeof(clientport), NI_NUMERICHOST|NI_NUMERICSERV);
	int64_t timestamp = (CTimer::getTime() - m_StartTime) / 60000000; // secret changes every one minute
	stringstream cookiestr;
	cookiestr << clienthost << ":" << clientport << ":" << timestamp;
	unsigned char cookie[16];
	CMD5::compute(cookiestr.str().c_str(), cookie);

	if (1 == hs.m_iReqType)
	{
		hs.m_iCookie = *(int*)cookie;
		packet.m_iID = hs.m_iID;
		int size = packet.getLength();
		hs.serialize(packet.m_pcData, size);
		m_pSndQueue->sendto(addr, packet);
		return 0;
	}
	else
	{
		if (hs.m_iCookie != *(int*)cookie)
		{
			timestamp --;
			cookiestr << clienthost << ":" << clientport << ":" << timestamp;
			CMD5::compute(cookiestr.str().c_str(), cookie);

			if (hs.m_iCookie != *(int*)cookie)
				return -1;
		}
	}

	int32_t id = hs.m_iID;

	// When a peer side connects in...
	if ((1 == packet.getFlag()) && (0 == packet.getType()))
	{
		if ((hs.m_iVersion != m_iVersion) || (hs.m_iType != m_iSockType))
		{
			// mismatch, reject the request
			hs.m_iReqType = 1002;
			int size = CHandShake::m_iContentSize;
			hs.serialize(packet.m_pcData, size);
			packet.m_iID = id;
			m_pSndQueue->sendto(addr, packet);
		}
		else
		{
			int result = s_UDTUnited.newConnection(m_SocketID, addr, &hs);
			if (result == -1)
				hs.m_iReqType = 1002;

			// send back a response if connection failed or connection already existed
			// new connection response should be sent in connect()
			if (result != 1)
			{
				int size = CHandShake::m_iContentSize;
				hs.serialize(packet.m_pcData, size);
				packet.m_iID = id;
				m_pSndQueue->sendto(addr, packet);
			}
			else
			{
				// a mew connection has been created, enable epoll for write
				s_UDTUnited.m_EPoll.enable_write(m_SocketID, m_sPollID);
			}
		}
	}

	return hs.m_iReqType;
}

void CUDT::checkTimers()
{
	// update CC parameters
	m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
	m_dCongestionWindow = m_pCC->m_dCWndSize;
	//uint64_t minint = (uint64_t)(m_ullCPUFrequency * m_pSndTimeWindow->getMinPktSndInt() * 0.9);
	//if (m_ullInterval < minint)
	//   m_ullInterval = minint;

	uint64_t currtime;
	CTimer::rdtsc(currtime);

	if ((currtime > m_ullNextACKTime) || ((m_pCC->m_iACKInterval > 0) && (m_pCC->m_iACKInterval <= m_iPktCount)))
	{
		// ACK timer expired or ACK interval is reached

		sendCtrl(2);
		CTimer::rdtsc(currtime);
		if (m_pCC->m_iACKPeriod > 0)
			m_ullNextACKTime = currtime + m_pCC->m_iACKPeriod * m_ullCPUFrequency;
		else
			m_ullNextACKTime = currtime + m_ullACKInt;

		m_iPktCount = 0;
		m_iLightACKCount = 1;
	}
	else if (m_iSelfClockInterval * m_iLightACKCount <= m_iPktCount)
	{
		//send a "light" ACK
		sendCtrl(2, NULL, NULL, 4);
		++ m_iLightACKCount;
	}

	// we are not sending back repeated NAK anymore and rely on the sender's EXP for retransmission
	//if ((m_pRcvLossList->getLossLength() > 0) && (currtime > m_ullNextNAKTime))
	//{
	//   // NAK timer expired, and there is loss to be reported.
	//   sendCtrl(3);
	//
	//   CTimer::rdtsc(currtime);
	//   m_ullNextNAKTime = currtime + m_ullNAKInt;
	//}

	uint64_t next_exp_time;
	if (m_pCC->m_bUserDefinedRTO)
		next_exp_time = m_ullLastRspTime + m_pCC->m_iRTO * m_ullCPUFrequency;
	else
	{
		uint64_t exp_int = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + m_iSYNInterval) * m_ullCPUFrequency;
		if (exp_int < m_iEXPCount * m_ullMinExpInt)
			exp_int = m_iEXPCount * m_ullMinExpInt;
		next_exp_time = m_ullLastRspTime + exp_int;
	}

	if (currtime > next_exp_time)
	{
		// Haven't receive any information from the peer, is it dead?!
		// timeout: at least 16 expirations and must be greater than 10 seconds
		if ((m_iEXPCount > 16) && (currtime - m_ullLastRspTime > 5000000 * m_ullCPUFrequency))
		{
			//
			// Connection is broken.
			// UDT does not signal any information about this instead of to stop quietly.
			// Application will detect this when it calls any UDT methods next time.
			//
			m_bClosing = true;
			m_bBroken = true;
			m_iBrokenCounter = 30;

			// update snd U list to remove this socket
			//cout<<"checktimer update"<<endl;
			m_pSndQueue->m_pSndUList->update(this);

			releaseSynch();

			// app can call any UDT API to learn the connection_broken error
			s_UDTUnited.m_EPoll.enable_read(m_SocketID, m_sPollID);
			s_UDTUnited.m_EPoll.enable_write(m_SocketID, m_sPollID);

			CTimer::triggerEvent();

			return;
		}

		// sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
		// recver: Send out a keep-alive packet
		if (m_pSndBuffer->getCurrBufSize() > 0)
		{
			if ((CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && (m_pSndLossList->getLossLength() == 0))
			{
				// resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
				int32_t csn = m_iSndCurrSeqNo;
				int num = m_pSndLossList->insert(const_cast<int32_t&>(m_iSndLastAck), csn);
				m_iTraceSndLoss += num;
				m_iSndLossTotal += num;
                                cout<<"insert"<<endl;
			}

			m_pCC->onTimeout(1, 1, 1, 1, -1, 1);
			// update CC parameters
			m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
			m_dCongestionWindow = m_pCC->m_dCWndSize;

			// immediately restart transmission
			//cout<<"retrans update"<<endl;
			m_pSndQueue->m_pSndUList->update(this);
		}
		else
		{
			sendCtrl(1);
		}

		++ m_iEXPCount;
		// Reset last response time since we just sent a heart-beat.
		m_ullLastRspTime = currtime;
	}
}

void CUDT::addEPoll(const int eid)
{
	CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
	m_sPollID.insert(eid);
	CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

	if (!m_bConnected || m_bBroken || m_bClosing)
		return;

	if ((UDT_STREAM == m_iSockType) && (m_pRcvBuffer->getRcvDataSize() > 0))
		s_UDTUnited.m_EPoll.enable_read(m_SocketID, m_sPollID);
	else if ((UDT_DGRAM == m_iSockType) && (m_pRcvBuffer->getRcvMsgNum() > 0))
		s_UDTUnited.m_EPoll.enable_read(m_SocketID, m_sPollID);

	if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
		s_UDTUnited.m_EPoll.enable_write(m_SocketID, m_sPollID);
}

void CUDT::removeEPoll(const int eid)
{
	CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
	m_sPollID.erase(eid);
	CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

	// clear IO events notifications;
	// since this happens after the epoll ID has been removed, they cannot be set again
	s_UDTUnited.m_EPoll.disable_read(m_SocketID, m_sPollID);
	s_UDTUnited.m_EPoll.disable_write(m_SocketID, m_sPollID);
}

double CUDT::get_min_rtt() const {
	double min = 0;
	if ((m_last_rtt.size()) > 0) {
		min = *m_last_rtt.begin();
		for (deque<double>::const_iterator it = m_last_rtt.begin(); it!=m_last_rtt.end(); ++it) {
			if (min > *it) {
				min = *it;
			}
		}
	}
	if (min == 0) return last_rtt_;
	return min;
}

void CUDT::start_monitor(int length)
{
	lock_guard<mutex> lck(monitor_mutex_);
	//cout << "start monitor!" << endl;
	m_iMonitorCurrSeqNo=0;
	previous_monitor = current_monitor;
	current_monitor = (current_monitor+1)%100;

	//int tmp = (current_monitor + 96) % 100;
	//int count = 0;

	//ygi: hack here!
    int suggested_length = 100000;
    int mss = m_iMSS;
	m_pCC->onMonitorStart(current_monitor, suggested_length, mss);
    if(mss != m_iMSS) {
        this->resizeMSS(mss);
    }
	m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
    time_interval[current_monitor] = m_pCC->m_dPktSndPeriod;
    //double rand_factor = double(rand()%10)/100.0;
	//if(m_iRTT*(1.2)/m_pCC->m_dPktSndPeriod>10) length = m_iRTT*(0.5 + rand_factor)/m_pCC->m_dPktSndPeriod;
		//cout << "min RTT is " << get_min_rtt() << endl;
		allocated_times_[current_monitor] = 2.0 * get_min_rtt();//last_rtt_;//m_iRTT;//get_min_rtt();
                if(allocated_times_[current_monitor]> 1000000) {
                    allocated_times_[current_monitor] = 1000000;
                }
				if (allocated_times_[current_monitor]< kMinTimeoutMillis) {
                    allocated_times_[current_monitor] = kMinTimeoutMillis;
                }
		//cout << "m_iRTT: " << m_iRTT << ". Min RTT = " << get_min_rtt() << endl;
		//cout << "monitor " << current_monitor << ", deadline is " << deadlines[current_monitor] << " --> " << x << endl;
	m_monitor_count++;

	//double rand_factor = (rand() %10) / 100.;
	int send_period = 1.0*m_iRTT; //100 * 1000; // 100 milliseconds
	//length = send_period*(0.5 + rand_factor)/m_pCC->m_dPktSndPeriod;
            if(send_period > 1000000) {
               send_period = 300000;
            }

	if(send_period/m_pCC->m_dPktSndPeriod>2) {
            length = send_period/m_pCC->m_dPktSndPeriod;
        }
	else {
            //cout<<"super short length because of short sent period? send period is "<< send_period<<endl;
            length=4;
        }
        if (length > 25) {
           length = 25;
        }
        //cout<<"length of monitor is "<<length<<endl;
    if (suggested_length < length) {
        length = suggested_length;
    }

//#ifdef EXPERIMENTAL_FEATURE_CONTINOUS_SEND
	//	length=50000/m_pCC->m_dPktSndPeriod;
// length = 10;
// #endif
	// add the transmition time
	//if (length > 100) length = 100;
	deadlines[current_monitor] = CTimer::getTime() + allocated_times_[current_monitor] + length * m_pCC->m_dPktSndPeriod;
    //cerr<<"start monitor "<<current_monitor<< " at:"<<CTimer::getTime()<<" with allocated timeout of "<<allocated_times_[current_monitor]<<" and send period of "        <<length * m_pCC->m_dPktSndPeriod<<endl;
	state[current_monitor] = 1;
	latency[current_monitor]=0;
	test=1;

	//	if (monitor_ttl>0)
	//		end_monitor(false);
	monitor_ttl = length;
	left[current_monitor]=0;
    rtt_count[current_monitor]=0;
    rtt_value[current_monitor]=0;
	//cout <<"length = " << length << endl;

	for (int i=0;i<length;++i)
    {
        recv_ack[current_monitor][i] = false;
        send_timestamp[current_monitor][i] = 0;
    }
	lost[current_monitor] = 0;
        latency_time_start[current_monitor] = 0;
        latency_time_end[current_monitor] = 0;
        latency_seq_end[current_monitor] = 0;
        latency_seq_start[current_monitor] = 0;
	retransmission[current_monitor] = 0;
	total[current_monitor] = length;
	new_transmission[current_monitor] = 0;
	start_time[current_monitor] = CTimer::getTime();
	end_transmission_time[current_monitor] = -1;
	monitor = true;
}

void CUDT::init_state() {
	current_monitor = 0;
	loss_record1.clear();
	loss_record2.clear();
	for (unsigned int mon_index = 0; mon_index < 100; mon_index++) {
		state[mon_index] = 3;
		total[mon_index] = 0;
		lost[mon_index] = 0;
		retransmission[mon_index] = 0;
		new_transmission[mon_index] = 0;
		latency[mon_index] = 0;
		latency_seq_end[mon_index] = 0;
		latency_time_start[mon_index] = 0;
		latency_time_end[mon_index] = 0;
		time_interval[mon_index] = 0;
		rtt_count[mon_index] = 0;
		rtt_value[mon_index] = 0;
		deadlines[mon_index] = 0;
		allocated_times_[mon_index] = 0;
	}
	monitor = true;
	left_monitor = 0;
	m_monitor_count = 0;
	m_iRTT = 10 * m_iSYNInterval;
	//return;
	for (unsigned int i = 0; i < 5; i++) {
		m_last_rtt[i] = 5 * m_iSYNInterval;
	}
	cout << "initialized " << 5 << " in the m_last_rtt to be "<< 5 * m_iSYNInterval << endl;


	//if (m_pSndLossList) delete m_pSndLossList;
	//m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);

	//if (m_pRcvLossList) delete m_pRcvLossList;
	//m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);

}

bool CUDT::timeout_monitors() {
	lock_guard<mutex> lck(monitor_mutex_);
	uint64_t current_time = CTimer::getTime();
	int tmp = (current_monitor + 1) % 100;
	while (tmp != current_monitor) {
		if ((state[tmp]==1) || (state[tmp]==2)) {
			if((deadlines[tmp] < current_time) && (allocated_times_[tmp] > 0)) {
				int count=0;
				//cout<<"killing "<<tmp<<" at "<<current_time<<endl;
				cout << "waited more than " << allocated_times_[tmp] <<endl;
				m_monitor_count = 0;
				for(int i=0;i<total[tmp];i++){
					if(recv_ack[tmp][i]){
						count++;
					}
				}
				if(count>0) latency[tmp] /= count;
				state[tmp] = 3;
				lost[tmp]=total[tmp]-left[tmp];
				end_time[tmp] = current_time;
				left_monitor--;
				m_pCC->onTimeout(total[tmp],total[tmp]-left[tmp],(end_transmission_time[tmp]-start_time[tmp])/1000000,current_monitor,tmp, allocated_times_[tmp]/1000);
				m_iRTT = allocated_times_[tmp];
	            loss_record1.clear();
	            loss_record2.clear();
	            for (unsigned int mon_index = 0; mon_index < 100; mon_index++) {
	            	state[mon_index] = 3;
	            	total[mon_index] = 0;
	            	lost[mon_index] = 0;
	            	retransmission[mon_index] = 0;
	            	new_transmission[mon_index] = 0;
	            	latency[mon_index] = 0;
	            	latency_seq_end[mon_index] = 0;
	            	latency_time_start[mon_index] = 0;
	            	latency_time_end[mon_index] = 0;
	            	time_interval[mon_index] = 0;
	            	rtt_count[mon_index] = 0;
	            	rtt_value[mon_index] = 0;
	            	deadlines[mon_index] = 0;
	            	allocated_times_[mon_index] = 0;
					m_last_rtt.clear();
	            }

	            monitor = true;
	            left_monitor = 0;
	            m_monitor_count = 0;
                return true;
			}
		}
    tmp = (tmp + 1) % 100;
	}
    return false;
}

void CUDT::save_timeout_time() {
	cout << "saving to file" << endl;

	std::ofstream outfile("/home/yossi/timeout_times.txt", std::ios_base::app);
	outfile << "timeout at time " << time(NULL) - start_ <<  ". Last RTTs: ";
	for(unsigned int i = 0; (i < m_last_rtt.size()) && (i < 10); i++) {
		outfile << m_last_rtt[i] << ", ";
	}
	outfile << endl;
}

double CUDT::estimate_rtt_for_timedout_monitors(int monitor) {
	return allocated_times_[monitor];
}
