//
//  main.c
//  Opee
//
//  Created by Alex Zielenski on 7/20/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <sys/param.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/sysctl.h>
#include <security/mac.h>

enum {
    kCFLogLevelEmergency = 0,
    kCFLogLevelAlert = 1,
    kCFLogLevelCritical = 2,
    kCFLogLevelError = 3,
    kCFLogLevelWarning = 4,
    kCFLogLevelNotice = 5,
    kCFLogLevelInfo = 6,
    kCFLogLevelDebug = 7,
};

CF_EXPORT void CFLog(int32_t level, CFStringRef format, ...);
CF_EXPORT CFURLRef CFCopyHomeDirectoryURLForUser(CFStringRef uName);

#define OPLogLevelNotice kCFLogLevelNotice
#define OPLogLevelWarning kCFLogLevelWarning
#define OPLogLevelError kCFLogLevelError

#define OPLog(TYPE, fmt, ...) CFLog(TYPE, CFSTR("Opee: " fmt), ##__VA_ARGS__)

#define kOPFiltersKey CFSTR("OPFilters")

const char *OPLibrariesPath = "/Library/Opee/DynamicLibraries";

// pretty much all of this we borrowed from MobileSubstrate to get the
// same expected functionality of the filtering
__attribute__((__constructor__)) static void _OpeeInit(){
    if (dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_NOLOAD) == NULL)
        return;
    
    // The first argument is the spawned process
    // Get the process name by looking at the last path
    // component.
    char argv[MAXPATHLEN];
    unsigned int buffSize = MAXPATHLEN;
    _NSGetExecutablePath(argv, &buffSize);
    
    char *executable = strrchr(argv, '/');
    executable = (executable == NULL) ? argv : executable + 1;
    
    /* Blacklisted Process Names
     These are blacklisted because they are
     use internally by the logic below which would
     result in a crash at boot
     */
#define BLACKLIST(PROCESS) if (strcmp(executable, #PROCESS) == 0) return;
    BLACKLIST(notifyd);
    BLACKLIST(configd);
    BLACKLIST(coreservicesd);
    BLACKLIST(opendirectoryd);
    
    /*
     These are processes which are blacklisted because they break
     some system functionality
     */
    BLACKLIST(PluginProcess);
    BLACKLIST(ssh);
    BLACKLIST(ksfetch);

    // Blacklisting report crash so crash reports are generated
    BLACKLIST(ReportCrash);
    // Blacklist MDWorker for performance reasons
    BLACKLIST(mdworker);
    
    // Don't load in safe mode:
    int safeBoot;
    int mib_name[2] = { CTL_KERN, KERN_SAFEBOOT };
    size_t length = sizeof(safeBoot);
    if (!sysctl(mib_name, 2, &safeBoot, &length, NULL, 0)) {
        if (safeBoot == 1) {
            // We are in safe mode
            return;
        } else {
            // Normal mode. Continue…
        }
    } else {
        // Couldn't find safe boot flag
        return;
    }
    
    // dont load into root processes
    struct passwd *pw = getpwuid(getuid());
    if (pw->pw_name == NULL || strcmp(pw->pw_name, "root") == 0)
        return;
    // only load in processes run by a user with a home dir
    else if (pw->pw_dir == NULL || strlen(pw->pw_dir) == 0)
        return;
    
    if (access(OPLibrariesPath, X_OK | R_OK) == -1) {
        return;
    }
    
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    CFStringRef identifier = (mainBundle == NULL) ? NULL : CFBundleGetIdentifier(mainBundle);
    OPLog(OPLogLevelNotice, "Installing %@ [%s] (%.2f)", identifier, executable, kCFCoreFoundationVersionNumber);
    
    
    CFURLRef libraries = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                                 (const UInt8 *)OPLibrariesPath,
                                                                 strlen(OPLibrariesPath),
                                                                 true);
    if (libraries == NULL)
        return;

    CFBundleRef folder = CFBundleCreate(kCFAllocatorDefault, libraries);
    CFRelease(libraries);
    
    if (folder == NULL)
        return;
    
    CFArrayRef bundles = CFBundleCopyResourceURLsOfType(folder, CFSTR("bundle"), NULL);
    CFRelease(folder);
    
    if (bundles == NULL) {
        return;
    }
    
    for (CFIndex i = 0; i < CFArrayGetCount(bundles); i++) {
        CFURLRef bundleURL = CFArrayGetValueAtIndex(bundles, i);
        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundleURL);
        CFDictionaryRef info = CFBundleGetInfoDictionary(bundle);
        CFDictionaryRef filters = CFDictionaryGetValue(info, kOPFiltersKey);
        
        bool shouldLoad = false;

        if (filters == NULL) {
            goto release;
        }

        CFArrayRef versionFilter = CFDictionaryGetValue(filters, CFSTR("CoreFoundationVersion"));

        if (versionFilter != NULL) {
            shouldLoad = false;
            CFIndex count = CFArrayGetCount(versionFilter);
            if (count > 2) {
                goto release;
            }

            CFNumberRef number;
            double value;

            number = CFArrayGetValueAtIndex(versionFilter, 0);
            if (CFGetTypeID(number) != CFNumberGetTypeID() || !CFNumberGetValue(number, kCFNumberDoubleType, &value)) {
                goto release;
            }

            if (value > kCFCoreFoundationVersionNumber)
                goto release;

            if (count != 1) {
                number = CFArrayGetValueAtIndex(versionFilter, 1);
                if (CFGetTypeID(number) != CFNumberGetTypeID() || !CFNumberGetValue(number, kCFNumberDoubleType, &value)) {
                    goto release;
                }

                if (value <= kCFCoreFoundationVersionNumber)
                    goto release;
            }

            shouldLoad = true;
        }

        // This makes it so only one filter has to match
        bool any;
        CFStringRef mode = CFDictionaryGetValue(filters, CFSTR("Mode"));

        if (mode)
            any = CFEqual(mode, CFSTR("Any"));
        else
            any = false;

        if (any)
            shouldLoad = false;

        CFArrayRef executableFilter = CFDictionaryGetValue(filters, CFSTR("Executables"));

        if (executableFilter) {
            if (!any)
                shouldLoad = false;

            CFStringRef executableName = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                                                         executable,
                                                                         kCFStringEncodingUTF8,
                                                                         kCFAllocatorNull);
            for (CFIndex i = 0; i < CFArrayGetCount(executableFilter); i++) {
                CFStringRef name = CFArrayGetValueAtIndex(executableFilter, i);
                if (CFEqual(executableName, name)) {
                    shouldLoad = true;
                    break;
                }
            }

            CFRelease(executableName);

            if (!any && !shouldLoad)
                goto release;
        }
 
        CFArrayRef bundlesFilter = CFDictionaryGetValue(filters, CFSTR("Bundles"));

        if (bundlesFilter != NULL) {
            if (!any)
                shouldLoad = false;

            for (CFIndex i = 0; i < CFArrayGetCount(bundlesFilter); i++) {
                CFStringRef bundleName = CFArrayGetValueAtIndex(bundlesFilter, i);
                if (CFBundleGetBundleWithIdentifier(bundleName) != NULL) {
                    shouldLoad = true;
                    break;
                }
            }

            if (!any && !shouldLoad)
                goto release;
        }

        CFArrayRef classesFilter = CFDictionaryGetValue(filters, CFSTR("Classes"));

        if (classesFilter != NULL) {
            if (!any)
                shouldLoad = false;
            
            Class (*NSClassFromString)(CFStringRef) = dlsym(RTLD_DEFAULT, "NSClassFromString");
            if (NSClassFromString != NULL) {
                for (CFIndex i = 0; i < CFArrayGetCount(classesFilter); i++) {
                    CFStringRef class = CFArrayGetValueAtIndex(classesFilter, i);
                    if (NSClassFromString(class) != NULL) {
                        shouldLoad = true;
                        break;
                    }
                }
            }

            if (!any && !shouldLoad)
                goto release;
        }

    release:
        // move on to the next dylib if filters don't match
        if (!shouldLoad) {
            CFRelease(bundle);
            continue;
        }

        // CFBundleLoad doesn't use the correct dlopen flags
        CFURLRef executableURL = CFBundleCopyExecutableURL(bundle);
        const char executablePath[PATH_MAX];
        CFURLGetFileSystemRepresentation(executableURL, true, (UInt8*)&executablePath, PATH_MAX);
        CFRelease(executableURL);
        
        // load the dylib
        void *handle = dlopen(executablePath, RTLD_LAZY | RTLD_GLOBAL);
        if (handle == NULL) {
            OPLog(OPLogLevelError, "%s", dlerror());
        }
        
        CFRelease(bundle);
    }

    CFRelease(bundles);
}
