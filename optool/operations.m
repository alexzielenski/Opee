//
//  operations.m
//  Opee
//
//  Created by Alexander S Zielenski on 7/22/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#import "operations.h"
#import "NSData+Reading.h"

unsigned int OP_SOFT_STRIP = 0x00001337;

BOOL stripCodeSignatureFromBinary(NSMutableData *binary, struct thin_header macho, BOOL softStrip) {
    binary.currentOffset = macho.offset + macho.size;
    BOOL success = NO;

    // Loop through the commands until we found an LC_CODE_SIGNATURE command
    // and either replace it and its corresponding signature with zero-bytes
    // or change LC_CODE_SIGNATURE to OP_SOFT_STRIP, so the compiler
    // can't interpret the load command for the code signature and treats
    // the binary as if it doesn't exist
    for (int i = 0; i < macho.header.ncmds; i++) {
        if (binary.currentOffset >= binary.length ||
            binary.currentOffset > macho.header.sizeofcmds + macho.size + macho.offset) // dont go past the header
            break;
        
        uint32_t cmd  = [binary intAtOffset:binary.currentOffset];
        uint32_t size = [binary intAtOffset:binary.currentOffset + sizeof(uint32_t)];
        
        switch (cmd) {
            case LC_CODE_SIGNATURE: {
                struct linkedit_data_command command = *(struct linkedit_data_command *)(binary.bytes + binary.currentOffset);
                LOG("stripping code signature for architecture %s...", CPU(macho.header.cputype));
                
                if (!softStrip) {
                    macho.header.ncmds -= 1;
                    macho.header.sizeofcmds -= sizeof(struct linkedit_data_command);
                    [binary replaceBytesInRange:NSMakeRange(command.dataoff, command.datasize) withBytes:0 length:command.datasize];
                    [binary replaceBytesInRange:NSMakeRange(binary.currentOffset, sizeof(struct linkedit_data_command)) withBytes:0 length:0];
                    [binary replaceBytesInRange:NSMakeRange(macho.offset + macho.header.sizeofcmds + macho.size, 0) withBytes:0 length:size];
                } else {
                    [binary replaceBytesInRange:NSMakeRange(binary.currentOffset, 4)
                                      withBytes:&OP_SOFT_STRIP];
                }
                
                success = YES;
                break;
            }
            default:
                binary.currentOffset += size;
                break;
        }
    }

    // paste in a modified header with an updated number and size of load commands
    if (!softStrip) {
        [binary replaceBytesInRange:NSMakeRange(macho.offset, sizeof(macho.header)) withBytes:&macho.header length:sizeof(macho.header)];
    }
    
    return success;
}

BOOL removeLoadEntryFromBinary(NSMutableData *binary, struct thin_header macho, NSString *payload) {
    // parse load commands to see if our load command is already there
    binary.currentOffset = macho.offset + macho.size;
    
    uint32_t num = 0;
    uint32_t cumulativeSize = 0;
    for (int i = 0; i < macho.header.ncmds; i++) {
        if (binary.currentOffset >= binary.length ||
            binary.currentOffset > macho.offset + macho.size + macho.header.sizeofcmds)
            break;
        
        uint32_t cmd  = [binary intAtOffset:binary.currentOffset];
        uint32_t size = [binary intAtOffset:binary.currentOffset + 4];
        
        // delete the bytes in all of the load commands matching the description
        switch (cmd) {
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_LOAD_DYLIB: {
                struct dylib_command command = *(struct dylib_command *)(binary.bytes + binary.currentOffset);
                char *name = (char *)[[binary subdataWithRange:NSMakeRange(binary.currentOffset + command.dylib.name.offset, command.cmdsize - command.dylib.name.offset)] bytes];
                if ([@(name) isEqualToString:payload]) {
                    LOG("removing payload from %s...", LC(cmd));
                    // remove load command
                    // remove these bytes and append zeroes to the end of the header
                    [binary replaceBytesInRange:NSMakeRange(binary.currentOffset, size) withBytes:0 length:0];
                    num++;
                    cumulativeSize += size;
                }
                
                binary.currentOffset += size;
                break;
            }
            default:
                binary.currentOffset += size;
                break;
        }
    }
    
    if (num == 0)
        return NO;
    
    // fix the header
    macho.header.ncmds -= num;
    macho.header.sizeofcmds -= cumulativeSize;
    
    unsigned int zeroByte = 0;
    
    // append a null byte for every one we removed to the end of the header
    [binary replaceBytesInRange:NSMakeRange(macho.offset + macho.header.sizeofcmds + macho.size, 0) withBytes:&zeroByte length:cumulativeSize];
    [binary replaceBytesInRange:NSMakeRange(macho.offset, sizeof(macho.header))
                      withBytes:&macho.header
                         length:sizeof(macho.header)];
    
    return YES;
}

BOOL binaryHasLoadCommandForDylib(NSMutableData *binary, NSString *dylib, uint32_t *lastOffset, struct thin_header macho) {
    binary.currentOffset = macho.size + macho.offset;
    unsigned int loadOffset = (unsigned int)binary.currentOffset;

    // Loop through compatible LC_LOAD commands until we find one which points
    // to the given dylib and tell the caller where it is and if it exists
    for (int i = 0; i < macho.header.ncmds; i++) {
        if (binary.currentOffset >= binary.length ||
            binary.currentOffset > macho.offset + macho.size + macho.header.sizeofcmds)
            break;
        
        uint32_t cmd  = [binary intAtOffset:binary.currentOffset];
        uint32_t size = [binary intAtOffset:binary.currentOffset + 4];
        
        switch (cmd) {
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_LOAD_DYLIB: {
                struct dylib_command command = *(struct dylib_command *)(binary.bytes + binary.currentOffset);
                char *name = (char *)[[binary subdataWithRange:NSMakeRange(binary.currentOffset + command.dylib.name.offset, command.cmdsize - command.dylib.name.offset)] bytes];
                
                if ([@(name) isEqualToString:dylib]) {
                    *lastOffset = (unsigned int)binary.currentOffset;
                    return YES;
                }
                
                binary.currentOffset += size;
                loadOffset = (unsigned int)binary.currentOffset;
                break;
            }
            default:
                binary.currentOffset += size;
                break;
        }
    }
    
    if (lastOffset != NULL)
        *lastOffset = loadOffset;
    
    return NO;
}

BOOL insertLoadEntryIntoBinary(NSString *dylibPath, NSMutableData *binary, struct thin_header macho, uint32_t type) {
    if (type != LC_REEXPORT_DYLIB &&
        type != LC_LOAD_WEAK_DYLIB &&
        type != LC_LOAD_UPWARD_DYLIB &&
        type != LC_LOAD_DYLIB) {
        LOG("Invalid load command type");
        return NO;
    }
    // parse load commands to see if our load command is already there
    uint32_t lastOffset = 0;
    if (binaryHasLoadCommandForDylib(binary, dylibPath, &lastOffset, macho)) {
        // there already exists a load command for this payload so change the command type
        uint32_t originalType = *(uint32_t *)(binary.bytes + lastOffset);
        if (originalType != type) {
            LOG("A load command already exists for %s. Changing command type from %s to desired %s", dylibPath.UTF8String, LC(originalType), LC(type));
            [binary replaceBytesInRange:NSMakeRange(lastOffset, sizeof(type)) withBytes:&type];
        } else {
            LOG("Load command already exists");
        }
        
        return YES;
    }
    
    // create a new load command
    unsigned int length = (unsigned int)sizeof(struct dylib_command) + (unsigned int)dylibPath.length;
    unsigned int padding = (8 - (length % 8));
    
    // check if data we are replacing is null
    NSData *occupant = [binary subdataWithRange:NSMakeRange(macho.header.sizeofcmds + macho.offset + macho.size,
                                                            length + padding)];

    // All operations in optool try to maintain a constant byte size of the executable
    // so we don't want to append new bytes to the binary (that would break the executable
    // since everything is offset-based–we'd have to go in and adjust every offset)
    // So instead take advantage of the huge amount of padding after the load commands
    if (strcmp([occupant bytes], "\0")) {
        NSLog(@"cannot inject payload into %s because there is no room", dylibPath.fileSystemRepresentation);
        return NO;
    }
    
    LOG("Inserting a %s command for architecture: %s", LC(type), CPU(macho.header.cputype));
    
    struct dylib_command command;
    struct dylib dylib;
    dylib.name.offset = sizeof(struct dylib_command);
    dylib.timestamp = 2; // load commands I've seen use 2 for some reason
    dylib.current_version = 0;
    dylib.compatibility_version = 0;
    command.cmd = LC_LOAD_UPWARD_DYLIB;
    command.dylib = dylib;
    command.cmdsize = length + padding;
    
    unsigned int zeroByte = 0;
    NSMutableData *commandData = [NSMutableData data];
    [commandData appendBytes:&command length:sizeof(struct dylib_command)];
    [commandData appendData:[dylibPath dataUsingEncoding:NSASCIIStringEncoding]];
    [commandData appendBytes:&zeroByte length:padding];
    
    // remove enough null bytes to account of our inserted data
    [binary replaceBytesInRange:NSMakeRange(macho.offset + macho.header.sizeofcmds + macho.size, commandData.length)
                      withBytes:0
                         length:0];
    // insert the data
    [binary replaceBytesInRange:NSMakeRange(lastOffset, 0) withBytes:commandData.bytes length:commandData.length];
    
    // fix the existing header
    macho.header.ncmds += 1;
    macho.header.sizeofcmds += command.cmdsize;
    
    // this is safe to do in 32bit because the 4 bytes after the header are still being put back
    [binary replaceBytesInRange:NSMakeRange(macho.offset, sizeof(macho.header)) withBytes:&macho.header];
    
    return YES;
}
BOOL removeASLRFromBinary(NSMutableData *binary, struct thin_header macho) {
    // MH_PIE is a flag on the macho header whcih indicates that the address space of the executable
    // should be randomized
    if (macho.header.flags & MH_PIE) {
        macho.header.flags &= ~MH_PIE;
        [binary replaceBytesInRange:NSMakeRange(macho.offset, sizeof(macho.header)) withBytes:&macho.header];
    } else {
        LOG("binary is not protected by ASLR");
        return NO;
    }
    
    return YES;
}
