//
//  main.cpp
//  mach-patcher
//
//  Created by Ruslan Gumennyi on 02/04/14.
//  Copyright (c) 2014 Ruslan Gumennyi. All rights reserved.
//

#include <iostream>
#include <libgen.h>
#include <stdint.h>
#include <sys/_endian.h>

#include "loader.h"
#include "fat.h"

void patchMachHeader(void *mem, uint32_t offset, char *injectedLibName)
{
    mach_header *header = (mach_header *)((uint8_t *)mem + offset);
    int commandsCount = header->ncmds;
    printf("Commands count: %d\n", commandsCount);
    
    dylib_command *lastDylibCommand = NULL;
    load_command *command = (load_command *)((uint8_t *)header + sizeof(mach_header));
    for (int i = 0; i < commandsCount; i++) {
        if (command->cmd == LC_LOAD_DYLIB) {
            dylib_command *typedCommand = (dylib_command *)command;
            lastDylibCommand = typedCommand;
            printf("LC_LOAD_DYLIB: %s\n", (char *)((uint8_t *)typedCommand + typedCommand->dylib.name.offset));
        }
        command = (load_command *)((uint8_t *)command + command->cmdsize);
    }
    
    if (lastDylibCommand) {
        int libNameSize = (int)strlen(injectedLibName) + 1;
        int padding = 4 - libNameSize % 4;
        
        dylib_command injectedCommand;
        injectedCommand.cmd = LC_LOAD_DYLIB;
        injectedCommand.cmdsize = 24 + libNameSize + padding;
        injectedCommand.dylib.current_version = 0x10000;
        injectedCommand.dylib.compatibility_version = 0x10000;
        injectedCommand.dylib.timestamp = 0;
        injectedCommand.dylib.name.offset = 24;
        
        uint8_t *newCommandPos = (uint8_t *)lastDylibCommand + lastDylibCommand->cmdsize;
        long bytesForMoving = header->sizeofcmds + sizeof(mach_header) - (int)((uint8_t *)newCommandPos - (uint8_t *)header);
        
        memmove((uint8_t *)newCommandPos + injectedCommand.cmdsize, newCommandPos, bytesForMoving);
        memcpy(newCommandPos, &injectedCommand, sizeof(injectedCommand));
        memcpy((uint8_t *)newCommandPos + sizeof(injectedCommand), injectedLibName, libNameSize);
        
        header->ncmds += 1;
        header->sizeofcmds += injectedCommand.cmdsize;
    }
}

int main(int argc, const char * argv[])
{
    if (argc != 3) {
        char *appName = basename((char *)(argv[0]));
        printf("Usage: %s path_to_binary inject_lib\n", appName);
        printf("Example: %s Payload/Test.app/Test @executable_path/libTestComplete-agent-dylib.dylib\n", appName);
        return 1;
    }
        
    char *binaryFileName = (char *)argv[1];
    char *injectedLibName = (char *)argv[2];
    
    FILE *fileToken = fopen(binaryFileName, "r+b");
    if (!fileToken) {
        printf("File open error: %d \n", errno);
        return 1;
    }
    
    fseek(fileToken, 0, SEEK_END);
    long fileSize = ftell(fileToken);
    fseek(fileToken, 0, SEEK_SET);
    void *buf = malloc(fileSize);
    fread(buf, 1, fileSize, fileToken);
    fclose(fileToken);
    
    if (*(uint32_t *)buf == FAT_CIGAM) {
        fat_header *fatHeader = (fat_header *)buf;
        int archCount = ntohl(fatHeader->nfat_arch);
        printf("Archs count: %d\n", archCount);

        for (int i = 0; i < archCount; i++) {
            fat_arch *fatArch = (fat_arch *)((uint8_t *)fatHeader + sizeof(fat_header) + sizeof(fat_arch) * i);
            printf("CPU type: %d\nCPU subtype: %d\nOffset: %d\nSize: %d\nAlign: %d\n",
                   ntohl(fatArch->cputype), ntohl(fatArch->cputype), ntohl(fatArch->offset), ntohl(fatArch->size), ntohl(fatArch->align));
            
            patchMachHeader(buf, ntohl(fatArch->offset), injectedLibName);
        }
    } else if (*(uint32_t *)buf == MH_MAGIC) {
        patchMachHeader(buf, 0, injectedLibName);
    }
    
    fileToken = fopen(binaryFileName, "wb");
    fwrite(buf, 1, fileSize, fileToken);
    fclose(fileToken);
    
    free(buf);
    
    return 0;
}

