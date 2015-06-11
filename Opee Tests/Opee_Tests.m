//
//  Opee_Tests.m
//  Opee Tests
//
//  Created by Alexander S Zielenski on 7/22/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Opee/Opee.h>
#import <XCTest/XCTest.h>

OPHook0(NSString *, NSUserName) {
    return @"No";
}

@interface Opee_Tests : XCTestCase @end

@implementation Opee_Tests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
    OPHookFunction(NSUserName);
}

- (void)testHooking {
    XCTAssertEqualObjects(NSUserName(), @"No");
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}
@end