//
//  main.m
//  opinject
//
//  Created by Alexander S Zielenski on 7/22/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "FSArgumentParser/ArgumentParser/FSArguments.h"

#import "defines.h"
#import "headers.h"
#import "operations.h"

int main(int argc, const char * argv[]) {    
    @autoreleasepool {
        // Flags
        FSArgumentSignature *weak = [FSArgumentSignature argumentSignatureWithFormat:@"[-w --weak]"];
        FSArgumentSignature *resign = [FSArgumentSignature argumentSignatureWithFormat:@"[--resign]"];
        FSArgumentSignature *target = [FSArgumentSignature argumentSignatureWithFormat:@"[-t --target]={1,1}"];
        FSArgumentSignature *payload = [FSArgumentSignature argumentSignatureWithFormat:@"[-p --payload]={1,1}"];
        FSArgumentSignature *command = [FSArgumentSignature argumentSignatureWithFormat:@"[-c --command]={1,1}"];
        FSArgumentSignature *backup = [FSArgumentSignature argumentSignatureWithFormat:@"[-b --backup]"];
        FSArgumentSignature *output = [FSArgumentSignature argumentSignatureWithFormat:@"[-o --output]={1,1}"];
        
        // Actions
        FSArgumentSignature *strip = [FSArgumentSignature argumentSignatureWithFormat:@"[s strip]"];
        FSArgumentSignature *restore = [FSArgumentSignature argumentSignatureWithFormat:@"[r restore]"];
        FSArgumentSignature *install = [FSArgumentSignature argumentSignatureWithFormat:@"[i install]"];
        FSArgumentSignature *uninstall = [FSArgumentSignature argumentSignatureWithFormat:@"[u uninstall]"];
        FSArgumentSignature *aslr = [FSArgumentSignature argumentSignatureWithFormat:@"[a aslr]"];
        
        [strip setInjectedSignatures:[NSSet setWithObjects:target, weak, nil]];
        [restore setInjectedSignatures:[NSSet setWithObjects:target, nil]];
        [install setInjectedSignatures:[NSSet setWithObjects:target, payload, nil]];
        [uninstall setInjectedSignatures:[NSSet setWithObjects:target, payload, nil]];
        [aslr setInjectedSignatures:[NSSet setWithObjects:target, nil]];
        
        FSArgumentPackage * package = [[NSProcessInfo processInfo] fsargs_parseArgumentsWithSignatures:@[resign, command, strip, restore, install, uninstall, output, backup, aslr]];
        
        NSString *targetPath = [package firstObjectForSignature:target];
        
        NSBundle *bundle = [NSBundle bundleWithPath:targetPath];
        NSString *executablePath = [[bundle.executablePath ?: targetPath stringByExpandingTildeInPath] stringByResolvingSymlinksInPath];
        NSString *backupPath = ({
            NSString *bkp = [executablePath stringByAppendingString:@"_backup"];
            if (bundle) {
                NSString *vers = [bundle objectForInfoDictionaryKey:(NSString *)kCFBundleVersionKey];
                if (vers)
                    bkp = [bkp stringByAppendingPathExtension:vers];
                
            }
            bkp;
        });;

        NSString *outputPath = [package firstObjectForSignature:output] ?: executablePath;
        NSString *dylibPath  = [package firstObjectForSignature:payload];
        
        NSFileManager *manager = [NSFileManager defaultManager];
        
        if ([package booleanValueForSignature:restore]) {
            LOG("Attempting to restore %s...", backupPath.UTF8String);
            
            if ([manager fileExistsAtPath:backupPath]) {
                NSError *error = nil;
                if ([manager removeItemAtPath:executablePath error:&error]) {
                    if ([manager moveItemAtPath:backupPath toPath:executablePath error:&error]) {
                        LOG("Successfully restored backup");
                        return 0;
                    }
                    LOG("Failed to move backup to correct location");
                    return OPErrorMoveFailure;
                }
                
                LOG("Failed to remove executable. (%s)", error.localizedDescription.UTF8String);
                return OPErrorRemovalFailure;
            }
            
            LOG("No backup for that target exists");
            return OPErrorNoBackup;
        }
        NSData *originalData = [NSData dataWithContentsOfFile:executablePath];
        NSMutableData *binary = originalData.mutableCopy;
        if (!binary)
            return OPErrorRead;
        
        struct thin_header headers[4];
        uint32_t numHeaders = 0;
        headersFromBinary(headers, binary, &numHeaders);
        
        if (numHeaders == 0) {
            LOG("No compatible architecture found");
            return OPErrorIncompatibleBinary;
        }
        for (uint32_t i = 0; i < numHeaders; i++) {
            struct thin_header macho = headers[i];

            if ([package booleanValueForSignature:strip]) {
                if (!stripCodeSignatureFromBinary(binary, macho, [package booleanValueForSignature:weak])) {
                    LOG("Found no code signature to strip");
                    return OPErrorStripFailure;
                } else {
                    LOG("Successfully stripped code signatures");
                }
            } else if ([package booleanValueForSignature:uninstall]) {
                if (removeLoadEntryFromBinary(binary, macho, dylibPath)) {
                    LOG("Successfully removed all entries for %s", dylibPath.UTF8String);
                } else {
                    LOG("No entries for %s exist to remove", dylibPath.UTF8String);
                    return OPErrorNoEntries;
                }
            } else if ([package booleanValueForSignature:install]) {
                NSString *lc = [package firstObjectForSignature:command];
                uint32_t command = LC_LOAD_DYLIB;
                if (lc)
                    command = COMMAND(lc);
                if (command == -1) {
                    LOG("Invalid load command.");
                    return OPErrorInvalidLoadCommand;
                }
                
                if (insertLoadEntryIntoBinary(dylibPath, binary, macho, command)) {
                    LOG("Successfully inserted a %s command for %s", LC(command), CPU(macho.header.cputype));
                } else {
                    LOG("Failed to insert a %s command for %s", LC(command), CPU(macho.header.cputype));
                    return OPErrorInsertFailure;
                }
            } else if ([package booleanValueForSignature:aslr]) {
                LOG("Attempting to remove ASLR");
                if (removeASLRFromBinary(binary, macho)) {
                    LOG("Successfully removed ASLR from binary");
                }
            }
        }
        
        if ([package booleanValueForSignature:backup]) {
            NSError *error = nil;
            LOG("Backing up executable (%s)...", executablePath.UTF8String);
            if (![manager copyItemAtPath:executablePath toPath:backupPath error:&error]) {
                LOG("Encountered error during backup: %s", error.localizedDescription.UTF8String);
                return OPErrorBackupFailure;
            }
        }
        
        LOG("Writing executable to %s...", outputPath.UTF8String);
        if (![binary writeToFile:outputPath atomically:NO]) {
            LOG("Failed to write data. Permissions?");
            return OPErrorWriteFailure;
        }
        
        if ([package booleanValueForSignature:resign]) {
            LOG("Attempting to resign %s...", bundle ? bundle.bundlePath.UTF8String : executablePath.UTF8String);
            NSPipe *output = [NSPipe pipe];
            NSTask *task = [[NSTask alloc] init];
            task.launchPath = @"/usr/bin/codesign";
            task.arguments = @[ @"-f", @"-s", @"-", bundle ? bundle.bundlePath : executablePath ];
            
            [task setStandardOutput:output];
            [task setStandardError:output];
            [task launch];
            [task waitUntilExit];
            
            NSFileHandle *read = [output fileHandleForReading];
            NSData *dataRead = [read readDataToEndOfFile];
            NSString *stringRead = [[NSString alloc] initWithData:dataRead encoding:NSUTF8StringEncoding];
            LOG("%s", stringRead.UTF8String);
            if (task.terminationStatus == 0) {
                LOG("Successfully resigned executable");
            } else {
                LOG("Failed to resign executable. Reverting...");
                [originalData writeToFile:executablePath atomically:NO];
                return OPErrorResignFailure;
            }
        }
    }
    
    return 0;
}


