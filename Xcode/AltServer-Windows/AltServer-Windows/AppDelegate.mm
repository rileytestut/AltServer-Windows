//
//  AppDelegate.m
//  AltServer-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#import "AppDelegate.h"

#include "AppleAPI.hpp"

#include "ConnectionManager.hpp"

@interface AppDelegate ()

@property (weak) IBOutlet NSWindow *window;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    ConnectionManager::instance()->Start();
    
//    auto task = AppleAPI::getInstance()->Authenticate("rileytestut+impactor@gmail.com", "fX8zsUdZWR")
//    .then([=](std::shared_ptr<Account> account)
//          {
//              return AppleAPI::getInstance()->FetchTeams(account);
//          })
//    .then([=](std::vector<std::shared_ptr<Team>> teams)
//          {
//              return AppleAPI::getInstance()->FetchDevices(teams[0]);
//          });
//
//    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
//        try
//        {
//            auto devices = task.get();
//            std::cout << "Devices:" << std::endl;
//
//            for (auto &device : devices)
//            {
//                std::cout << *device << std::endl;
//            }
//        }
//        catch (const Error &error)
//        {
//            std::cout << "Error: " << error.localizedDescription() << std::endl;
//        }
//        catch (const std::exception &exception)
//        {
//            std::cout << "Exception: " << exception.what() << std::endl;
//        }
//    });
}

std::string make_uuid()
{
    NSString *uuidString = [[NSUUID UUID] UUIDString];
    return std::string(uuidString.UTF8String);
}

std::string temporary_directory()
{
    return [[NSFileManager defaultManager] temporaryDirectory].fileSystemRepresentation;
}

std::vector<unsigned char> readFile(const char* filename)
{
    // open the file:
    std::ifstream file(filename, std::ios::binary);
    
    // Stop eating new lines in binary mode!!!
    file.unsetf(std::ios::skipws);
    
    // get its size:
    std::streampos fileSize;
    
    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // reserve capacity
    std::vector<unsigned char> vec;
    vec.reserve(fileSize);
    
    // read the data:
    vec.insert(vec.begin(),
               std::istream_iterator<unsigned char>(file),
               std::istream_iterator<unsigned char>());
    
    return vec;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}


@end
