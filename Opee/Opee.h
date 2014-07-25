//
//  Opee.h
//  Opee
//
//  Created by Alexander S Zielenski on 7/22/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#import <Cocoa/Cocoa.h>

//! Project version number for Opee.
FOUNDATION_EXPORT double OpeeVersionNumber;

//! Project version string for Opee.
FOUNDATION_EXPORT const unsigned char OpeeVersionString[];

// Creates a function which is executed when the library loads
#define OPInitialize __attribute__((__constructor__)) static void _OPInitialize()

#import <Opee/ZKSwizzle.h>
#import <Opee/OPHooker.h>