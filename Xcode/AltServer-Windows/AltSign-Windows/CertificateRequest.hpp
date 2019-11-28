//
//  CertificateRequest.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef CertificateRequest_hpp
#define CertificateRequest_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <string>
#include <vector>

class CertificateRequest
{
public:
    CertificateRequest();
    ~CertificateRequest();
    
    std::vector<unsigned char> data() const;
    std::vector<unsigned char> privateKey() const;
    
private:
    std::vector<unsigned char> _data;
    std::vector<unsigned char> _privateKey;
};

#pragma GCC visibility pop

#endif /* CertificateRequest_hpp */
