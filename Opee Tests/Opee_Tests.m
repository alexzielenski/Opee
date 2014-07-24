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

@interface OPOriginalClass : NSObject
+ (NSString *)classMethod;
+ (NSString *)description;
- (NSString *)instanceMethod;
- (NSString *)description;
@end

@implementation OPOriginalClass

+ (BOOL)isSubclassOfClass:(Class)aClass { return YES; }
+ (NSString *)classMethod { return @"original"; }
+ (NSString *)description { return @"original"; }
- (NSString *)instanceMethod { return @"original"; }
- (NSString *)description { return @"original"; }

@end

OPHook0(NSString *, NSUserName) {
    return [$O_NSUserName() stringByAppendingString: @"_replaced"];
}

@interface OPSwizzleClass : OPOriginalClass @end
@implementation OPSwizzleClass

+ (BOOL)isSubclassOfClass:(Class)aClass {
    return (BOOL)ORIG();
}

- (NSString *)className {
    return [ORIG() stringByAppendingString:@"_replaced"];
}

+ (NSString *)classMethod {
    return @"replaced";
}

+ (NSString *)description {
    return SUPER();
}

- (NSString *)instanceMethod {
    return @"replaced";
}

- (NSString *)description {
    return SUPER();
}

@end



@interface Opee_Tests : XCTestCase @end

@implementation Opee_Tests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
    [OPSwizzler swizzleClass:[OPSwizzleClass class] forClass:[OPOriginalClass class]];
    OPHookFunction(NSUserName);
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testSwizzle {
    OPOriginalClass *instance = [[OPOriginalClass alloc] init];
    XCTAssertEqualObjects([OPOriginalClass classMethod], @"replaced", @"replacing class methods");
    XCTAssertEqualObjects([instance instanceMethod], @"replaced", @"replacing instance methods");
    XCTAssertNotEqualObjects([OPOriginalClass description], @"original", @"calling super on class");
    XCTAssertNotEqualObjects([instance description], @"original", @"calling super on instance");
    XCTAssertEqual([OPOriginalClass isSubclassOfClass:[NSString class]], YES, @"calling super imp on class");
    XCTAssertEqualObjects([instance className], @"OPOriginalClass_replaced", @"calling original imp on instance");
    XCTAssertEqualObjects(NSUserName(), [NSHomeDirectory().lastPathComponent stringByAppendingString:@"_replaced"], @"function hooking");
}

@end
