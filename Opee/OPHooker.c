//
//  OPHooker.c
//  Opee
//
//  Created by Alexander S Zielenski on 7/23/14.
//  Copyright (c) 2014 Alex Zielenski. All rights reserved.
//

#include "OPHooker.h"
void MSHookFunction(void *symbol, void *replace, void **result);
void *MSFindSymbol(void *image, const char *name);

int OPHookFunctionPtr(void *symbol, void *replace, void **result) {
    MSHookFunction(symbol, replace, result);
    return 0;
}

void *OPFindSymbol(const char *name) {
    return MSFindSymbol(NULL, name);
}