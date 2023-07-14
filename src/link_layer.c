// Link layer protocol implementation
#include "../include/link_layer.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>


#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256
#define F 0x7E
#define ESC 0x7D
#define ESCF 0x5E
#define ESCE 0x5D
#define A 0x03
#define A_LLCLOSE 0x01
#define UA 0x07 
#define SET 0x03
#define DISC 0x0B
#define RR0	0x05
#define RR1	0x85
#define REJ1 0x81
#define REJ0 0x01
#define BCC(a,b) a^b
#define FALSE 0
#define TRUE 1

#define S_0 0x00
#define S_1 0x40

unsigned char NS = S_0;
unsigned char NR = 1;
unsigned char ctrlField = 0;


unsigned int nTries, timeout;
struct termios oldtio, newtio;
char role;
int fd;
int reject = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

int statemachine(int *state, int *over, unsigned char ctrlByte, unsigned char AType){
    unsigned char c;
    int bytes = read(fd, &c, 1);
    if(bytes == -1){
        return -1;
    }
    if(bytes == 0){
        return 0;
    }

    switch (*state){
        case 0:
            if(c == F){
                *state = 1;
            }
            return 0;
        case 1:
            if(c == F){
                return 0;
            }
            else if(c == A){
                *state = 2;
            }
            else{
                *state = 0;
            }
            return 0;
        case 2:
            if(c == F){
                *state = 1;
            }
            else if(c == ctrlByte){
                *state = 3;
            }
            else{
                *state = 0;
            }
            return 0;
        case 3:
            if(c == F){
                *state = 1;
            }
            else if(c == (BCC(A,ctrlByte))){
                *state = 4;
            }
            else{
                *state = 0;
            }
            return 0;
        case 4:
            if(c == F){
                *over = TRUE;
            }
            else{
                *state = 0;
            }
            return 0;
    }
}
  


int llopen(LinkLayer connectionParameters)
{
    nTries = connectionParameters.nRetransmissions;
    
    role = connectionParameters.role;
    timeout = connectionParameters.timeout;

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0){
        return -1;
    }

    
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

   
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0.1;
    newtio.c_cc[VMIN] = 0;  
    

    tcflush(fd, TCIOFLUSH);

   
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    if(role == LlTx){
        unsigned char tramaSET[5];
        tramaSET[0] = F;
        tramaSET[1] = A;
        tramaSET[2] = SET;
        tramaSET[3] = BCC(A,SET);
        tramaSET[4] = F;

        int over = FALSE;
        (void) signal(SIGALRM, alarmHandler);
        int state = 0;

        while(!over && alarmCount < nTries)
        {
            if (!alarmEnabled)
            {
                state = 0;
                if (write(fd, tramaSET, 5) == -1){
                    perror("failed to write tramaSET \n");
                    return -1;
                }
                alarm(timeout);
                alarmEnabled = TRUE;
            }

            if(statemachine(&state, &over, UA, A) == -1){
                perror("failed to read UA \n");
                return -1;
            }
        }
        if(!over){
            perror("nTries exceeded! \n");
            return -1;
        }
        alarmCount = 0;
        alarm(0);
        alarmEnabled = FALSE;
    }

    else if(role == LlRx){
        int state = 0;
        int over = FALSE;
        while(!over){
            if(statemachine(&state, &over, SET, A) == -1){
                perror("failed to read SET \n");
                return -1;
            }
        }
        unsigned char tramaUA[5];
        tramaUA[0] = F;
        tramaUA[1] = A;
        tramaUA[2] = UA;
        tramaUA[3] = BCC(A, UA);
        tramaUA[4] = F;

        if (write(fd, tramaUA, 5) == -1){
            perror("failed to write tramaUA\n");
            return -1;
        }
    }
    return fd;
}


int llwrite(const unsigned char *buf, int bufSize)
{
    (void) signal(SIGALRM, alarmHandler);
    reject = FALSE;
    unsigned int size =  2 * bufSize + 6;
    unsigned char data[size];

    data[0] = F;
    data[1] = A;
    data[2] = NS;
    data[3] = BCC(A,NS);
    int bcc2 = 0;
    int dataSize = 4;
    int i;
    for(i = 0; i < bufSize; i++) {
        
        if(buf[i] == F){ //stuffing
            data[dataSize] = ESC;
            data[dataSize+1] = ESCF;
            dataSize += 2;
        }
        else if(buf[i] == ESC){ //stuffing
            data[dataSize] = ESC;
            data[dataSize+1] = ESCE;
            dataSize += 2;
        }
            
        else{
            // no need to stuff
            data[dataSize] = buf[i];
            dataSize++;
        }
        bcc2 = bcc2 ^ buf[i]; 
        
    }
    
    if(bcc2 == F){  //stuffing bcc2
        data[dataSize] = ESC;
        data[dataSize+1] = ESCF;
        dataSize += 2;
    }
    else if(bcc2 == ESC){
        data[dataSize] = ESC;
        data[dataSize+1] = ESCE;
        dataSize += 2;
    }
    else{
        data[dataSize] = bcc2;
        dataSize++;
    }

    data[dataSize] = F;
    dataSize++;

    int over = FALSE;
    int state = 0;

    alarmEnabled = FALSE;
    while (alarmCount < nTries && !over) {
        if (!alarmEnabled){
            if(write(fd, data, dataSize) == -1){
                perror("writing data failed!\n");
                return -1;
            }
            state = 0;
            alarm(timeout);
            alarmEnabled = TRUE;
        }

        unsigned char c;
        int bytes = read(fd, &c, 1);

        if(bytes == -1){
            perror("read failed\n");
            continue;
        }
        if(bytes == 0){ //nothing to read   
            continue;
        }

        switch (state){
            case 0:
                if(c == F){
                    state = 1;
                }
                break;
            case 1:
                if(c == F){
                    continue;
                }
                else if(c == A){
                    state = 2;
                }
                else{
                    state = 0;
                }
                break;
            case 2:
                if(c == F){
                    state = 1;
                }
                else if (c == REJ0 || c == REJ1){
                    reject = TRUE;
                    printf("trama Rejected\n");
                    return 0;
                }
                else if(c == RR0 || c == RR1){
                    reject = FALSE;
                    ctrlField = c;
                    state = 3;
                }
                else{
                    state = 0;
                }
                break;
            case 3:
                if(c == F){
                    state = 1;
                }
                else if(c == (BCC(A,ctrlField))){
                    state = 4;
                }
                else{
                    state = 0;
                }
                break;
            case 4:
                if(c == F){
                    over = TRUE;
                }
                else{
                    state = 0;
                }
                break;

        }

    }


    if(!over){
        printf("nTries exceeded\n");
    }

    if(NS == S_0){
        NS = S_1;
    } else {
        NS = S_0;
    }

    alarmCount = 0;
    alarm(0);
    alarmEnabled = FALSE;

    return size;
}


int llread(unsigned char *packet)
{
    int over = FALSE;
    (void) signal(SIGALRM, alarmHandler);
    int state = 0;
    int escape = FALSE;
    int bcc2 = 0;
    int sizeData = 0;
    char ctrl;

    while(!over) {
        unsigned char c;
        int bytes = read(fd, &c, 1);
        if(bytes == -1){

            perror("failed reading\n");
            return -1;
        }
        if(bytes == 0){ //nothing to read
            continue;
        }

        switch (state){
            case 0:
                if(c == F){
                    state = 1;
                }
                break;
            case 1:
                if(c == F){
                    continue;
                }
                else if(c == A){
                    state = 2;
                }
                else{
                    state = 0;
                }
                break;
            case 2:
                if(c == F){
                    state = 1;
                }
                else if(c == S_0 || c == S_1){
                    ctrl = c;
                    state = 3;
                }
                else{
                    state = 0;
                }
                break;
            case 3:
                if(c == F){
                    state = 1;
                }
                else if(c == (BCC(A,ctrl))){ // bcc1
                    state = 4;
                }
                else{
                    printf("error in the protocol\n");
                    state = 0;
                }
                break;
            case 4:
                if(c != F){
                    //byte destuffing
                    if(c == ESC){
                        escape = TRUE;
                        continue;
                    }
                    if(escape){
                        if(c == ESCF){
                            packet[sizeData] = F;
                            sizeData++;
                        }
                        else if(c == ESCE){
                            packet[sizeData] = ESC;
                            sizeData++;
                        }
                        escape = FALSE;
                    }
                    else {
                        packet[sizeData] = c;
                        sizeData++;
                    }
                }
                else{
                    over = TRUE;
                }
                break;
        }
    }

    unsigned char newBCC2 = 0;
    bcc2 = packet[sizeData-1];
    sizeData--;
    for (int i=0; i<sizeData; i++) {
        newBCC2 ^= packet[i];
    }
    if (newBCC2 == bcc2) {
        unsigned char ack[5];
        ack[0] = F;
        ack[1] = A;
        ack[4] = F;
        switch (NR)
        {
        case 0:
            ack[2] = RR0;
            ack[3] = BCC(A, RR0);
            break;
        case 1:
            ack[2] = RR1;
            ack[3] = BCC(A, RR1);
            break;
        default:
            break;
        }

        if(write(fd,ack,5) == -1){
            printf("failed writing\n");
            return -1;
        }
    }
    else {
        printf("error in the data\n");
        unsigned char nack[5];
        nack[0] = F;
        nack[1] = A;
        nack[4] = F;
        switch (NR)
        {
        case 0:
            nack[2] = REJ1;
            nack[3] = BCC(A, REJ1);
            break;
        case 1:
            nack[2] = REJ0;
            nack[3] = BCC(A, REJ0);
            break;
        default:
            break;
        }

        if(write(fd,nack,5) == -1){
            printf("failed writing\n");
            return -1;
        }
    }

    if(NR == 0){
        NR = 1;
    } else {
        NR = 0;
    }

    return sizeData;
}

int llclose(int showStatistics)
{
    alarmCount = 0;
    alarm(0);
    alarmEnabled = FALSE;

    if(role == LlTx){
        unsigned char discC[5];
        discC[0] = F;
        discC[1] = A_LLCLOSE;
        discC[2] = DISC;
        discC[3] = BCC(A_LLCLOSE,DISC);
        discC[4] = F;

        int over = FALSE;
        (void) signal(SIGALRM, alarmHandler);
        int state = 0;

        while(!over && alarmCount < nTries )
        {
            if (!alarmEnabled)
            {
                state = 0;
                if (write(fd, discC, 5) == -1){
                    perror("failed writing\n");
                    return -1;
                }
                alarm(timeout);
                alarmEnabled = TRUE;
            }

            if (statemachine(&over, &state, DISC, A_LLCLOSE) == -1)
            {
                printf("error reading DISC\n");
                return -1;
            }
        }
        if(!over){
            printf("nTries exceeded!\n");
            return -1;
        }
        alarmCount = 0;
        alarm(0);
        alarmEnabled = FALSE;

        unsigned char UAValue[5];
        UAValue[0] = F;
        UAValue[1] = A_LLCLOSE;
        UAValue[2] = UA;
        UAValue[3] = BCC(A_LLCLOSE,UA);
        UAValue[4] = F;

        if (write(fd, UAValue, 5) == -1){
            perror("failed to write\n");
            return -1;
        }

    }

    else if(role == LlRx){
        int state = 0;
        int over = FALSE;
        while(!over){
            if (statemachine(&over, &state, DISC, A_LLCLOSE) == -1)
            {
                printf("error reading DISC\n");
                return -1;
            }

        }
        
        unsigned char disc[5];
        disc[0] = F;
        disc[1] = A_LLCLOSE;
        disc[2] = DISC;
        disc[3] = BCC(A_LLCLOSE, DISC);
        disc[4] = F;
        if (write(fd, disc, 5) == -1){
            perror("failed to write\n");
            return -1;
        }

        state = 0;
        over = FALSE;
        while(!over){
            if (statemachine(&over, &state, UA, A_LLCLOSE) == -1)
            {
                printf("error reading UA\n");
                return -1;
            }
        }
    }
    return fd;
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);
    return 1;
}
