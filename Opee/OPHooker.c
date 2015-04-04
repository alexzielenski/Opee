//
//  OPHooker.c
//  Opee
//
//  Created by Alexander S Zielenski on 7/23/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#include "OPHooker.h"
void MSHookFunction(void *symbol, void *replace, void **result);
void *MSFindSymbol(const void *image, const char *name);
void *MSGetImageByName(const char *file);

int OPHookFunctionPtr(void *symbol, void *replace, void **result) {
    MSHookFunction(symbol, replace, result);
    return 0;
}

void *OPGetImageByName(const char *file) {
    return MSGetImageByName(file);
}

void *OPFindSymbol(const void *image, const char *name) {
    return MSFindSymbol(image, name);
}