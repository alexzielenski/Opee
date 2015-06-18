# Opee

Opee is a platform which enables the ability to load dynamic libraries (dylibs) on Mac OS X into processes and override functionality.

The library must be contained by a bundle and be stored in `/Library/Opee/Extensions`. They are selected to be loaded into processes based upon a filter which is specified in their Info.plist.

I am not responsible for how much this screws up your system.

# Installation

Included is the source code for `Opee.framework`, `OpeeLoader.dylib`, and `optool`.

##### Make sure you have Apple's `Command Line Tools` package installed before continuing
You can find them [here](https://developer.apple.com/downloads/index.action#). Optionally, you can have OS X fetch them for you by running `codesign` in Terminal. If the tools are not installed, you will be prompted.

#### Procedure

***Note*** If you are using El Capitan, you must run these commands before starting installation:

```
sudo nvram boot-args="rootless=0"
sudo reboot
```
You can re-enable rootless after installation by replacing the `0` with a `1`.

1. Copy OpeeLoader.dylib to `/usr/lib`
2. Copy Opee.framework to `/Library/Frameworks`
3. Create the folder `/Library/Opee/Extensions`
4. Copy optool to `/usr/bin` 
5. optool can be used to install OpeeLoader:
6. Run in terminal:

		sudo optool install --backup --resign --command upward -t /System/Library/Frameworks/Foundation.framework -p /usr/lib/OpeeLoader.dylib
7. Opee is now installed. You can now use it to load dynamic libraries

### Notes

Opee modifies the Foundation.framework binary which is very tricky business. It tries to do its best to stay safe but sometimes there may be hiccups. **If you want or need to uninstall Opee do not simply remove its dylib**. It first needs to be removed from its hooks in Foundation. Instead, try one of the following options to restore functionality of your system:

1. **Opee is disabled in Safe Mode**. Try booting with the argument `-x` or by holding down the Shift key and then run the below command.
2. If you are booted, try running in Terminal:

		sudo optool restore -t /System/Library/Frameworks/Foundation.framework
3. If that's not an option or doesn't work you can try booting into the Recovery partition and running in Terminal:

		cd /System/Library/Frameworks/Foundation.framework/Versions/C
		// Find the location of the Opee backup with ls
		ls
		mv Foundation Foundation_evil
		mv (NAME OF BACKUP FOUND) Foundation
4. Boot into single user mode by holding CMD+S during boot, or use the terminal found in the Recovery partition to create the file `.OPSafeMode` in the root directory of your partition. (Dont forget the dot in `.OPSafeMode`!). Optionally, you make also place `.OPSafeMode` in `/Library/Opee/.OPSafeMode`
5. Otherwise, another solution is to boot into another OS and move the backup made by Opee to its original location, or restore Foundation.framework

# Components

### Opee.framework

This is the framework that extensions can link to in order to use Opee's method swizzling and function hooking APIs. Method swizzling is done using the Objective-C runtime, and the function hooking used was written for MobileSubstrate, which Opee was modeled after.

### OpeeLoader

OpeeLoader is a dyamic library that gets loaded into all processes that link Foundation.framework. It's job is to scan `/Library/Opee/Extensions` and load the extensions based upon their filters.

### optool

`optool` is a command line interface which is used during the installation process of Opee to patch an x86/x86_64 executable. It has 4 main functions:

1. strip – removes the code signature and corresponding load command from a fat/macho binary
2. install – adds a load dylib command to the path specified. Supports "reexport", "load", "weak", and "upward"
3. uninstall – removes all load commands to the specified path in the specified binary
4. restore – puts a backup made by the `install` command back. (The `install` command only makes backups if the `--backup` flag is set)

# Information

Place your extensions in `/Library/Opee/Extensions` or `~/Library/Opee/Extensions` to have them automatically loaded into processes depending upon thier filters.

You can choose to blacklist executables by placing a file called `OPConfig.plist` in either directory and specifying in an array called `Blacklist` the executable names or bundle identifiers which you'd like to blacklist.

Apps can choose to blacklist themselves by adding an `OPBlacklisted` key inside their `Info.plist` set to true. You can override this setting by adding their bundle identifier in your `OPConfig.plist` in the `Whitelist` array to have your extensions loaded into them anyway.

# Usage

Developers can easily make an extension by creating a new `Bundle` project in Xcode. Extensions can hook method and function calls either by their own way or by linking `Opee.framework` which includes functions and macros that are very useful for this purpose.

#### Configuration

1. Link Opee.framework
2. Set your install name base to /Library/Opee/Extensions
3. Configure your project to build for both I386 and X86_64 (Xcode nowadays chooses 64-bit by default)
4. Add filters to your Info.plist in the format specified below

## Hooking

`ZKSwizzle` is the class which can be used to hook Objective-C method calls and `OPHooker` is a collection of few C funcions and macros which allow the ability to hook C/C++/Swift functions. Their usage is as follows:


	@interface OriginalObject : NSObject
	@end
		
	// Define a class which we will swizzle
	@implementation OriginalObject
	+ (BOOL)isSubclassOfClass:(Class)aClass { return YES; }
	+ (NSString *)classMethod { return @"original"; }
	+ (NSString *)description { return @"original"; }
	- (NSString *)instanceMethod { return @"original"; }
	- (NSString *)description { return @"original"; }
	@end
		
	// All methods on this class which are present on the class that
	// it is swizzled to (including superclasses) are called instead of their
	// original implementation. The original implementaion can be accessed with the 
	// ORIG(...) macro and the implementation of the superclass of the class which
	// it was swizzled to can be access with the SUPER(...) macro
	@interface ReplacementObject : NSObject
	// Returns YES
	+ (BOOL)isSubclassOfClass:(Class)aClass { return (BOOL)ZKOrig(); }
	
	// Returns "original_replaced"
	- (NSString *)className { return [ZKOrig() stringByAppendingString:@"_replaced"]; }
	
	// Returns "replaced" when called on the OriginalObject class
	+ (NSString *)classMethod { return @"replaced"; }
	
	// Returns the default description implemented by NSObject	+ (NSString *)description { return SUPER(); }
	
	// Returns "replaced" when called on an instance of OriginalObject
	- (NSString *)instanceMethod { return @"replaced"; }
		
	// Returns the default description implemented by NSObject
	- (NSString *)description { return SUPER(); }
		
	// This method is added to instances of OriginalObject and can be called
	// like any normal function on OriginalObject
	- (void)addedMethod { NSLog(@"this method was added to OriginalObject"); }
	@end
		
	// When NSUserName() is called, it will return @"USERNAME_replaced"
	OPHook0(NSString *, NSUserName) {
	    return [OPOldCall() stringByAppendingString: @"_replaced"];
	}
		
	// This function is executed when the library is loaded
	OPInitialize {		
		ZKSwizzle(ReplacementObject, OriginalObject);
		OPHookFunction(NSUserName);
	}
	
Opee also has macros in place for hooking instance variables:

	// gets the value of _myIvar on self
	int myIvar = ZKHookIvar(self, int, "_myIvar");
	// gets the pointer to _myIvar on self so you can reassign it
	int *myIvar = &ZKHookIvar(self, int, "_myIvar");
	// set the value of myIvar on the object
	*myIvar = 3;

## Filters

In your Bundle's Info.plist, you must supply loading filters which go in a dictionary of key `OPFilters` these filters affect which processes will load your library. They are made to be compatible with MobileSubstrate filters for familiarity.

| Key                   | Type   | Value                                                                                                     |
|-----------------------|--------|-----------------------------------------------------------------------------------------------------------|
| CoreFoundationVersion | Array  | First object is the minimum CoreFoundation version and the Second is the maximum                          |
| Mode                  | String | Use "Any" for mode if you want to load if any of the below filters match rather than all.                 |
| Bundles               | Array  | List of Bundle identifiers to load into. This can be any type of bundle identifier. (e.g. framework, App) |
| Executables           | Array  | List of executable names to load into. Useful for loading into processes without a bundle                 |
| Classes               | Array  | Names of Objective-C classes whose presence will cause the library to load (e.g. NSBitmapImageRep)        |
