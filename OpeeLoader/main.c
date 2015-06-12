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
#include <syslog.h>
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

#define OPLogLevelNotice LOG_NOTICE
#define OPLogLevelWarning LOG_WARNING
#define OPLogLevelError LOG_ERR

#ifdef DEBUG
    #define OPLog(level, format, ...) do { \
        CFStringRef _formatted = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR(format), ## __VA_ARGS__); \
        size_t _size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(_formatted), kCFStringEncodingUTF8); \
        char _utf8[_size + sizeof('\0')]; \
        CFStringGetCString(_formatted, _utf8, sizeof(_utf8), kCFStringEncodingUTF8); \
        CFRelease(_formatted); \
        syslog(level, "Opee: " "%s", _utf8); \
    } while (false)

//    #define OPLog(TYPE, fmt, ...) CFLog(TYPE, CFSTR("Opee: " fmt), ##__VA_ARGS__)
#else
    #define OPLog(...) (void)1
#endif

//#define kOPFiltersKey CFSTR("OPFilters")
static const CFStringRef kOPFiltersKey    = CFSTR("OPFilters");
static const CFStringRef kSIMBLFiltersKey = CFSTR("SIMBLTargetApplications");
const char *OPLibrariesPath = "/Library/Opee/Extensions";
const char *OPSafePath      = "/.OPSafeMode";
const char *OPSafePath2     = "/Library/Opee/.OPSafeMode";

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
     used internally by the logic below which would
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
    BLACKLIST(mds);
    
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
        OPLog(OPLogLevelError, "Unable to access libraries directory");
        return;
    } else if (access(OPSafePath, R_OK) != -1 ||
               access(OPSafePath2, R_OK) != -1) {
        // The Safe Mode file exists, don't do anything
        OPLog(OPLogLevelNotice, "Safe Mode Enabled. Doing nothing.");
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
        if (bundle == NULL) {
            return;
        }
        
        CFDictionaryRef info = CFBundleGetInfoDictionary(bundle);
        if (info == NULL || CFGetTypeID(info) != CFDictionaryGetTypeID()) {
            goto release;
        }
        
        CFDictionaryRef filters = CFDictionaryGetValue(info, kOPFiltersKey);
        
        
        bool shouldLoad = false;

        if (filters == NULL || CFGetTypeID(filters) != CFDictionaryGetTypeID()) {
            // Try SIMBL?
            /*
             <key>SIMBLTargetApplications</key>
             <array>
             <dict>
             <key>BundleIdentifier</key>
             <string>com.apple.Safari</string>
             <key>MinBundleVersion</key>
             <integer>125</integer>
             <key>MaxBundleVersion</key>
             <integer>125</integer>
             </dict>
             </array>
             */
            
            CFArrayRef simblFilters = CFDictionaryGetValue(info, kSIMBLFiltersKey);
            if (simblFilters == NULL || CFGetTypeID(simblFilters) != CFArrayGetTypeID()) {
                goto release;
            }
            
            // we have some SIMBL filters
            CFIndex count = CFArrayGetCount(simblFilters);
            for (CFIndex i = 0; i < count && shouldLoad == false; i++) {
                CFDictionaryRef entry = CFArrayGetValueAtIndex(simblFilters, i);
                // only support dictionaries
                if (CFGetTypeID(entry) != CFDictionaryGetTypeID())
                    continue;
                
                CFStringRef bundleIdentifier = CFDictionaryGetValue(entry, CFSTR("BundleIdentifier"));
                if (bundleIdentifier == NULL || CFGetTypeID(bundleIdentifier) != CFStringGetTypeID())
                    continue;
                
                CFBundleRef bundle = NULL;
                if (CFEqual(bundleIdentifier, CFSTR("*"))) {
                    shouldLoad = true;
                    
                } else if ((bundle = CFBundleGetBundleWithIdentifier(bundleIdentifier)) != NULL) {
                    // we have a hit with the bundle identifier, check versions
                    // check min and max bundle versions
                    SInt32 targetVersion = CFBundleGetVersionNumber(bundle);
                    CFTypeRef minVersionObject = CFDictionaryGetValue(entry, CFSTR("MinBundleVersion"));
                    CFTypeRef maxVersionObject = CFDictionaryGetValue(entry, CFSTR("MaxBundleVersion"));
                    
                    // get the number out of whatever type it is in
                    SInt32 minVersion = 0;
                    if (minVersionObject != NULL) {
                        if (CFGetTypeID(minVersionObject) == CFNumberGetTypeID()) {
                            CFNumberGetValue(minVersionObject, kCFNumberSInt32Type, &minVersion);
                        } else if (CFGetTypeID(minVersionObject) == CFStringGetTypeID()) {
                            minVersion = CFStringGetIntValue(minVersionObject);
                        } else {
                            // Unrecognized version syntax
                            continue;
                        }
                    }
                    
                    SInt32 maxVersion = 0;
                    if (maxVersionObject != NULL) {
                        if (CFGetTypeID(maxVersionObject) == CFNumberGetTypeID()) {
                            CFNumberGetValue(maxVersionObject, kCFNumberSInt32Type, &maxVersion);
                        } else if (CFGetTypeID(maxVersionObject) == CFStringGetTypeID()) {
                            maxVersion = CFStringGetIntValue(maxVersionObject);
                        } else {
                            // Unrecognized version syntax
                            continue;
                        }
                    }
                    
                    // load if NOT (version exists and out of bounds)
                    shouldLoad = !((maxVersion && maxVersion < targetVersion) || (minVersion && minVersion > targetVersion));
                }
            }
            
            goto release;
        }

        CFArrayRef versionFilter = CFDictionaryGetValue(filters, CFSTR("CoreFoundationVersion"));

        if (versionFilter != NULL && CFGetTypeID(versionFilter) == CFArrayGetTypeID()) {
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

        if (executableFilter && CFGetTypeID(executableFilter) == CFArrayGetTypeID()) {
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

        if (bundlesFilter != NULL && CFGetTypeID(bundlesFilter) == CFArrayGetTypeID()) {
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

        if (classesFilter != NULL && CFGetTypeID(classesFilter) == CFArrayGetTypeID()) {
            if (!any)
                shouldLoad = false;
            
            for (CFIndex i = 0; i < CFArrayGetCount(classesFilter); i++) {
                CFStringRef class = CFArrayGetValueAtIndex(classesFilter, i);
                if (objc_getClass(CFStringGetCStringPtr(class, kCFStringEncodingUTF8)) != NULL) {
                    shouldLoad = true;
                    break;
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
