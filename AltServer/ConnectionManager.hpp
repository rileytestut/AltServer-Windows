//
//  ConnectionManager.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef ConnectionManager_hpp
#define ConnectionManager_hpp

#include <vector>
#include <thread>

#include "Connection.hpp"

class ConnectionManager
{
public:
    static ConnectionManager *instance();
    
    void Start();
    
private:
    ConnectionManager();
    ~ConnectionManager();
    
    static ConnectionManager *_instance;
    
    std::thread _listeningThread;
    
    int _mDNSResponderSocket;
    std::vector<std::shared_ptr<Connection>> _connections;
    
    int mDNSResponderSocket() const;
    std::vector<std::shared_ptr<Connection>> connections() const;
    
    void Listen();
    void StartAdvertising(int port);
    void ConnectToSocket(int socket, char *host, int port);
};

#endif /* ConnectionManager_hpp */
