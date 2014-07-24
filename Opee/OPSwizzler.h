//
//  OPSwizzler.h
//  Opee
//
//  Created by Alex Zielenski on 7/22/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// This is a class for streamlining swizzling. Simply create a new class of any name you want and
// this will swizzle any methods with a prefix defined in the +prefix method

// Example:
/*
 
 @interface OPHookClass : NSObject
 
 - (NSString *)description; // hooks -description on NSObject
 - (void)addedMethod; // all subclasses of NSObject now respond to -addedMethod

 @end
 
 @implementation OPHookClass

 ...
 
 @end
 
 [OPSwizzler swizzleClass:OPClass(OPHookClass) forClass:OPClass(destination)];
 
 this will swizzle fbclass_swizzledMethod to swizzledMethod on the destination class
 
 */

// Gets the a class with the name CLASS
#define OPClass(CLASS) objc_getClass(#CLASS)

// returns the value of an instance variable.
#define OPHookIvar(OBJECT, TYPE, NAME) (*(TYPE *)OPIvarPointer(OBJECT, NAME))
// returns the original implementation of the swizzled function or null or not found
#define ORIG(...) (OPOriginalImplementation(self, _cmd))(self, _cmd, ##__VA_ARGS__)
// returns the original implementation of the superclass of the object swizzled
#define SUPER(...) (OPSuperImplementation(self, _cmd))(self, _cmd, ##__VA_ARGS__)

#define OPSwizzle(SOURCE, DESTINATION) [OPSwizzler swizzleClass:OPClass(SOURCE) forClass:OPClass(DESTINATION)]
#define OPSwizzleClass(SOURCE) [OPSwizzler swizzleClass:OPClass(SOURCE)]

// thanks OBJC_OLD_DISPATCH_PROTOTYPES=0
typedef id (*OPIMP)(id, SEL, ...);

// returns a pointer to the instance variable "name" on the object
void *OPIvarPointer(id self, const char *name);
// returns the original implementation of a method with selector "sel" of an object hooked by the methods below
OPIMP OPOriginalImplementation(id object, SEL sel);
// returns the implementation of a method with selector "sel" of the superclass of object
OPIMP OPSuperImplementation(id object, SEL sel);

@interface OPSwizzler : NSObject
// hooks all the implemented methods of source with destination
// adds any methods that arent implemented on destination to destination that are implemented in source
+ (BOOL)swizzleClass:(Class)source forClass:(Class)destination;

// Calls above method with the superclass of source for desination
+ (BOOL)swizzleClass:(Class)source;
@end
