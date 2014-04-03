//
//  main.cpp
//  mach-patcher
//
//  Created by Ruslan Gumennyi on 02/04/14.
//  Copyright (c) 2014 Ruslan Gumennyi. All rights reserved.
//

#include <iostream>
#include <libgen.h>
#include "loader.h"

int main(int argc, const char * argv[])
{
    if (argc != 3) {
        char *appName = basename((char *)(argv[0]));
        printf("Usage: %s path_to_binary inject_lib\n", appName);
        printf("Example: %s Payload/Test.app/Test @executable_path/libTestComplete-agent-dylib.dylib\n", appName);
        return 1;
    }
        
    char *fileName = (char *)argv[1];
    char *injectLib = (char *)argv[2];
    
    FILE *fileToken = fopen(fileName, "r+b");
    if (!fileToken) {
        printf("File open error: %d \n", errno);
        return 1;
    }
    
    mach_header header;
    fread(&header, sizeof(header), 1, fileToken);
    long lastDylibPos = 0;
    long lastDylibSize = 0;
    
    for (int i = 0; i < header.ncmds; i++) {
        long commandPos = ftell(fileToken);
        
        load_command command;
        fread(&command, sizeof(command), 1, fileToken);

#ifdef DEBUG_INFO
        printf("cmd: %d size: %d\n", command.cmd, command.cmdsize);
#endif
        if (command.cmd == LC_LOAD_DYLIB) {
            lastDylibPos = commandPos;
            lastDylibSize = command.cmdsize;

#ifdef DEBUG_INFO
            dylib_command typedCommand;
            fseek(fileToken, commandPos, SEEK_SET);
            fread(&typedCommand, sizeof(dylib), 1, fileToken);
            
            int nameOfset = typedCommand.dylib.name.offset;
            int nameSize = typedCommand.cmdsize - nameOfset;
            
            fseek(fileToken, commandPos + nameOfset, SEEK_SET);
            char *name = (char *)calloc(nameSize, sizeof(char));
            fread(name, nameSize, 1, fileToken);
            printf("lib: %s\n", name);
            free(name);
#endif
        }
        
        fseek(fileToken, commandPos + command.cmdsize, SEEK_SET);
    }
    
    int libNameSize = (int)strlen(injectLib) + 1;
    int padding = 4 - libNameSize % 4;
    
    dylib_command injectedCommand;
    injectedCommand.cmd = LC_LOAD_DYLIB;
    injectedCommand.cmdsize = 24 + libNameSize + padding;
    injectedCommand.dylib.current_version = 0x10000;
    injectedCommand.dylib.compatibility_version = 0x10000;
    injectedCommand.dylib.timestamp = 0;
    injectedCommand.dylib.name.offset = 24;
    
    long newCommandPos = lastDylibPos + lastDylibSize;
    long bytesForMoving = header.sizeofcmds + sizeof(header) - newCommandPos;
    void *tmpBuf = calloc(bytesForMoving, 1);
    fseek(fileToken, newCommandPos, SEEK_SET);
    fread(tmpBuf, bytesForMoving, 1, fileToken);
    
    fseek(fileToken, newCommandPos, SEEK_SET);
    fwrite(&injectedCommand, sizeof(injectedCommand), 1, fileToken);
    fwrite(injectLib, libNameSize, 1, fileToken);
    
    fseek(fileToken, newCommandPos + injectedCommand.cmdsize, SEEK_SET);
    fwrite(tmpBuf, bytesForMoving, 1, fileToken);
    free(tmpBuf);
    
    header.ncmds += 1;
    header.sizeofcmds += injectedCommand.cmdsize;
    fseek(fileToken, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, fileToken);
    
    fclose(fileToken);
    
    return 0;
}

