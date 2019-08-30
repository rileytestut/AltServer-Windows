//
//  Signer.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Signer_hpp
#define Signer_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <string>
#include <vector>

#include "Team.hpp"
#include "Certificate.hpp"
#include "ProvisioningProfile.hpp"

class Signer
{
public:
    Signer(std::shared_ptr<Team> team, std::shared_ptr<Certificate> certificate);
    ~Signer();
    
    std::shared_ptr<Team> team() const;
    std::shared_ptr<Certificate> certificate() const;
    
    void SignApp(std::string appPath, std::vector<std::shared_ptr<ProvisioningProfile>> profiles);
    
private:
    std::shared_ptr<Team> _team;
    std::shared_ptr<Certificate> _certificate;
};

#pragma GCC visibility pop

#endif /* Signer_hpp */
