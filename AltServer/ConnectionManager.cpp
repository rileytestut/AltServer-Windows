//
//  ConnectionManager.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "ConnectionManager.hpp"

#include <iostream>
#include <thread>

//#include <netinet/in.h>

#include <stddef.h>

/*
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
*/

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "dns_sd.h"

#define odslog(msg) { std::wstringstream ss; ss << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }

void DNSSD_API ConnectionManagerBonjourRegistrationFinished(DNSServiceRef service, DNSServiceFlags flags, DNSServiceErrorType errorCode, const char *name, const char *regtype, const char *domain, void *context)
{
	std::cout << "Registered service: " << name << " (Error: " << errorCode << ")" << std::endl;
}

ConnectionManager* ConnectionManager::_instance = nullptr;

ConnectionManager* ConnectionManager::instance()
{
    if (_instance == 0)
    {
        _instance = new ConnectionManager();
    }
    
    return _instance;
}

ConnectionManager::ConnectionManager()
{
}

void ConnectionManager::Start()
{
	WSADATA wsaData;

	int iResult;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return;
	}

    auto listenFunction = [](void) {
        ConnectionManager::instance()->Listen();
    };
    
    _listeningThread = std::thread(listenFunction);
}

void ConnectionManager::Listen()
{
    std::cout << "Hello World" << std::endl;
    
    int socket4 = socket(AF_INET, SOCK_STREAM, 0);
    if (socket4 == 0)
    {
        std::cout << "Failed to create socket." << std::endl;
        return;
    }
    
    struct sockaddr_in address4;
    memset(&address4, 0, sizeof(address4));
    //address4.sin_len = sizeof(address4);
    address4.sin_family = AF_INET;
    address4.sin_port = 0; // Choose for us.
    address4.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(socket4, (struct sockaddr *)&address4, sizeof(address4)) < 0)
    {
        std::cout << "Failed to bind socket." << std::endl;
        return;
    }
    
    if (listen(socket4, 0) != 0)
    {
        std::cout << "Failed to prepare listening socket." << std::endl;
    }
    
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    
    if (getsockname(socket4, (struct sockaddr *)&sin, &len) == -1)
    {
        std::cout << "Failed to get socket name." << std::endl;
    }
    
    int port4 = ntohs(sin.sin_port);
    this->StartAdvertising(port4);
    
    fd_set input_set;
    fd_set copy_set;
    
    while (true)
    {
        struct timeval tv;
        tv.tv_sec = 1; /* 1 second timeout */
        tv.tv_usec = 0; /* no microseconds. */
        
        /* Selection */
        FD_ZERO(&input_set );   /* Empty the FD Set */
        FD_SET(socket4, &input_set);  /* Listen to the input descriptor */
        
        FD_ZERO(&copy_set );   /* Empty the FD Set */
        FD_SET(socket4, &copy_set);  /* Listen to the input descriptor */
        
        int ready_for_reading = select(socket4 + 1, &input_set, &copy_set, NULL, &tv);
        
        /* Selection handling */
        if (ready_for_reading > 0)
        {
			
            
			struct sockaddr_in clientAddress;
            memset(&clientAddress, 0, sizeof(clientAddress));

			int addrlen = sizeof(clientAddress);
            int other_socket = accept(socket4, (SOCKADDR*)&clientAddress, &addrlen);
            
			
            char *ipaddress = inet_ntoa(((struct sockaddr_in)clientAddress).sin_addr);
            int port2 = ntohs(((struct sockaddr_in)clientAddress).sin_port);
			int error = WSAGetLastError();

			odslog("Other Socket:" << other_socket << ". Port: " << port2 << ". Error: " << error);
            
            ConnectToSocket(other_socket, ipaddress, port2);            
        }
        else if (ready_for_reading == -1)
        {
             /* Handle the error */
            std::cout << "Uh-oh" << std::endl;
        }
        else
        {
           // Do nothing
        }
    }
}

void ConnectionManager::ConnectToSocket(int socket, char *host, int port)
{
    auto connection = std::make_shared<Connection>(socket);
    this->_connections.push_back(connection);
    
    connection->ProcessAppRequest().then([]() {
        std::cout << "Completed!" << std::endl;
    });
}

void ConnectionManager::StartAdvertising(int socketPort)
{
    DNSServiceRef service = NULL;
	uint16_t port = htons(socketPort);
    
    DNSServiceErrorType registrationResult = DNSServiceRegister(&service, 0, 0, NULL, "_altserver._tcp", NULL, NULL, port, 0, NULL, ConnectionManagerBonjourRegistrationFinished, NULL);
    if (registrationResult != kDNSServiceErr_NoError)
    {
        std::cout << "Bonjour Registration Error: " << registrationResult << std::endl;
        return;
    }
    
    int dnssd_socket = DNSServiceRefSockFD(service);
    if (dnssd_socket == -1)
    {
        std::cout << "Failed to retrieve mDNSResponder socket." << std::endl;
    }
    
    this->_mDNSResponderSocket = dnssd_socket;
}

int ConnectionManager::mDNSResponderSocket() const
{
    return _mDNSResponderSocket;
}

std::vector<std::shared_ptr<Connection>> ConnectionManager::connections() const
{
    return _connections;
}
