//
//  Connection.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Connection_hpp
#define Connection_hpp

#include <pplx/pplxtasks.h>
#include <cpprest/json.h>

#include <tuple>
#include <optional>

class Connection
{
public:
    Connection(int socket);
    ~Connection();
    
    int socket() const;
    
    pplx::task<void> ProcessAppRequest();

	pplx::task<void> ProcessPrepareAppRequest(web::json::value request);
	pplx::task<void> ProcessAnisetteDataRequest(web::json::value request);
    
private:
    int _socket;
    
	pplx::task<std::string> ReceiveApp(web::json::value request);
    pplx::task<void> InstallApp(std::string filepath, std::string udid);
    
    pplx::task<web::json::value> ReceiveRequest();
    pplx::task<std::vector<unsigned char>> ReceiveData(std::optional<int> size);
    
    pplx::task<void> SendResponse(web::json::value json);
    pplx::task<void> SendData(const std::vector<unsigned char>& data);
};

#endif /* Connection_hpp */
