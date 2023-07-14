// Application layer protocol implementation

#include "../include/application_layer.h"
#include "../include/link_layer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_DATA_SIZE (MAX_PAYLOAD_SIZE - 4)

extern int reject;

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);

    switch(role[0]) {
        case 't':
            linkLayer.role = LlTx;
            break;
        case 'r':
            linkLayer.role = LlRx;
            break;
        default:
            printf("Invalid role. Must be 'tx' or 'rx'.\n");
            return;
    }
    
    linkLayer.baudRate = baudRate;

    linkLayer.nRetransmissions = nTries;

    linkLayer.timeout = timeout;

    if(llopen(linkLayer) == -1){
        printf("failed llopen\n");
        return;
    }
    printf("llopen success\n");

    if(linkLayer.role == LlTx){
        FILE *file;
        file = fopen(filename, "rb");
        if (file == NULL) {
            printf("failed to open reading file\n");
            return;
        }
        fseek(file, 0, SEEK_END);
        unsigned int fileSzBytes = ftell(file); 
        fseek(file, 0, SEEK_SET);
        printf("file size: %d\n", fileSzBytes);
        unsigned int fileSize = fileSzBytes;

        unsigned int fileSizeStrSizeHex = 0;

        while(fileSize != 0){ // calculate size of file size string in hex
            fileSize >>= 8;
            fileSizeStrSizeHex++;
        }
        unsigned char fileSzHex[fileSizeStrSizeHex];
        int fileNameSize = strlen(filename);
        unsigned int ctrlPacketSize = 5 + fileSizeStrSizeHex + fileNameSize; 

        printf("first half of ctrl packet\n");
        unsigned char ctrlPacket[ctrlPacketSize];
        ctrlPacket[0] = 2; 
        ctrlPacket[1] = 0; 
        ctrlPacket[2] = fileSizeStrSizeHex;
        memcpy(&ctrlPacket[3], &fileSzBytes, fileSizeStrSizeHex);

        printf("second half of ctrl packet\n");
        ctrlPacket[3+fileSizeStrSizeHex] = 1; 
        ctrlPacket[4+fileSizeStrSizeHex] = fileNameSize;
        memcpy(&ctrlPacket[5+fileSizeStrSizeHex], filename, fileNameSize);


        printf("-------Writting strt ctrl pckt-------\n"); 
        if (llwrite(ctrlPacket, ctrlPacketSize) == -1){
            printf("failed llwrite\n");
            return;
        }


        printf("-------Writting data pckts-------\n"); 
        unsigned char dataPacket[MAX_PAYLOAD_SIZE] = {0};
        int size = 0;
        int data = TRUE;
        unsigned int j = 0;
        dataPacket[0] = 1;
        
        while (data){
            if (fileSzBytes > MAX_DATA_SIZE){
                fileSzBytes = fileSzBytes - MAX_DATA_SIZE;
                size = MAX_DATA_SIZE;
            }
            else{
                size = fileSzBytes;
                data = FALSE;
            }
            dataPacket[1] = j;
            dataPacket[2] = size / 256;
            dataPacket[3] = size % 256;

            unsigned char startCtrlPacket[size];
            fread(startCtrlPacket, 1, size, file);
            
            for (int i=0; i<size; i++){
                dataPacket[i+4] = startCtrlPacket[i];
            }

            j = (j+1)%255;  // increment sequence number up to 255
            int tries = 0;

            reject = TRUE;
            while (reject && tries < nTries) {
                reject = FALSE;
                if(llwrite(dataPacket, (int) (size+4)) == -1){
                    printf("failed llwrite\n");
                    return;
                }
                tries++;
            }
        }
        fclose(file);

        
        printf("-------Writting end ctrl pckt-------\n"); 
        ctrlPacket[0] = 3;
        if(llwrite(ctrlPacket, ctrlPacketSize) == -1){
            printf("failed llwrite\n");
            return;
        }

        printf("file sent\n");
    }

    else if(linkLayer.role == LlRx)
    {
        FILE *file;
        file = fopen(filename, "w+");
        if (file == NULL) {
            printf("failed to open file\n");
            return;
        }

        printf("-------Reading strt ctrl pckt-------\n"); 

        unsigned char startCtrlPacket[MAX_PAYLOAD_SIZE]={0};
        if(llread(startCtrlPacket) == -1){
            printf("failed reading the start control packet\n");
            return;
        }

        unsigned int fileSizeStrSizeHex = 0;
        unsigned int fileNameSize;
        unsigned char fileSzHex[MAX_PAYLOAD_SIZE];
        unsigned char ReceivedFileName[MAX_PAYLOAD_SIZE];
        int j = 0;

        printf("-------Reading data pckts-------\n"); 
        if(startCtrlPacket[0] == 2){
            if (startCtrlPacket[1] == 0) {
                fileSizeStrSizeHex = startCtrlPacket[2];
                
                for(int i = 0; i < fileSizeStrSizeHex; i++){
                    fileSzHex[i] = startCtrlPacket[3+i];
                }
            }
            if (startCtrlPacket[3+fileSizeStrSizeHex] == 1){ 
                fileNameSize = startCtrlPacket[4+fileSizeStrSizeHex];

                for(int i = 0; i < fileNameSize; i++){
                    ReceivedFileName[i] = startCtrlPacket[5+fileSizeStrSizeHex+i];
                }
        
            }
            
            while(TRUE){
                
                unsigned char packet[MAX_PAYLOAD_SIZE]={0};
                printf("-------Reading end ctrl pckt-------\n"); 
                if(llread(packet) == -1){
                    printf("failed reading the end control packet\n");
                    return;
                }
                

                if(packet[0] == 3){
                
                    if (packet[1] == 0) {
                        if(fileSizeStrSizeHex != packet[2]){
                            printf("file size string size is different!\n");
                            return;
                        }
                        for(int i = 0; i < fileSizeStrSizeHex; i++){
                            if(fileSzHex[i] != packet[3+i]){
                                printf("file size string is different!\n");
                                return;
                            }
                        }
                    }
                    if (packet[3+fileSizeStrSizeHex] == 1){ 
                        if(fileNameSize != packet[4+fileSizeStrSizeHex]){
                            printf("file name size is different!\n");
                            return;
                        }
                        for(int i = 0; i < fileNameSize; i++){
                            if(ReceivedFileName[i] != packet[5+fileSizeStrSizeHex+i]){
                                printf("file name is different!\n");
                                return;
                            }
                        }
                    }

                    break;
                }

                if(packet[0] == 1){

                    unsigned char N;
                    unsigned char L1, L2;
                    
                    N = packet[1];
                    if(N != j){
                        printf("sequence number is different!\n");
                        continue;
                    }
                    
                    L2 = packet[2]; 
                    L1 =  packet[3]; 
                    unsigned int size = L2*256 + L1;  // size of the data packet
                    j = (j+1)%255; // increment sequence number up to 255
                    unsigned char data[size];
                    for(int i = 0; i < size; i++){
                        data[i] = packet[4+i];
                    }
                    fwrite(data, 1, size, file);
                }

            }
        }
        fclose(file);
        printf("file received\n");
    }
    printf("-------Closing connection-------\n"); 
    if(llclose(0) == -1){
        printf("failed llclose\n");
    }
}
