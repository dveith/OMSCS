#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netdb.h>

#include "gfclient.h"

#define BUFSIZE 4096

struct gfcrequest_t  {
    char *server;
    char *path;
    unsigned short port;
    gfstatus_t status;
    char *statusText;
    size_t bytesreceived;
    size_t bytesOfFileReceived;
    size_t filelength;
    char *filelengthstring;
    void *writerargument;
    void *headerargument;
    void *chunk;
    void *header;
    size_t chunklength;
    size_t headerlength;
    void (*writerfunc)(void *, size_t, void *);
    void (*headerfunc)(void *, size_t, void *);
    void *fileContent;
};

gfcrequest_t *gfc_create(){
    struct gfcrequest_t *gfr = malloc(sizeof *gfr);
    return gfr;
}

void gfc_set_server(gfcrequest_t *gfr, char* server){
    gfr->server = server;
}

void gfc_set_path(gfcrequest_t *gfr, char* path){
    gfr->path = path;
}

void gfc_set_port(gfcrequest_t *gfr, unsigned short port){
    gfr->port = port;
}

void gfc_set_headerfunc(gfcrequest_t *gfr, void (*headerfunc)(void*, size_t, void *)){
    gfr->headerfunc = headerfunc;
}

void gfc_set_headerarg(gfcrequest_t *gfr, void *headerarg){
    gfr->headerargument = headerarg;
}

void gfc_set_writefunc(gfcrequest_t *gfr, void (*writefunc)(void*, size_t, void *)){
    gfr->writerfunc = writefunc;
}

void gfc_set_writearg(gfcrequest_t *gfr, void *writearg){
    gfr->writerargument = writearg;
}

int gfc_perform(gfcrequest_t *gfr){
    int clientSocket = 0;
    struct sockaddr_in serverSocketAddress;
    char receivedData[BUFSIZE];
    memset(receivedData, '0', sizeof(receivedData));
    char *fullData = "";
    int set_reuse_addr = 1;
    int headerComplete = 0;
    
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    
    setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, &set_reuse_addr, sizeof(set_reuse_addr));
    
    struct hostent *he = gethostbyname(gfr->server);
    unsigned long server_addr_nbo = *(unsigned long *)(he->h_addr_list[0]);
    
    bzero(&serverSocketAddress, sizeof(serverSocketAddress));
    serverSocketAddress.sin_family = AF_INET;
    serverSocketAddress.sin_port = htons(gfr->port);
    serverSocketAddress.sin_addr.s_addr = server_addr_nbo;
    
    connect(clientSocket, (struct sockaddr *)&serverSocketAddress, sizeof(serverSocketAddress));
    
    char * message = (char *) malloc(22 + strlen(gfr->path) );
    strcpy(message, "GETFILE GET ");
    strcat(message, gfr->path);
    strcat(message, " \r\n\r\n");
    
    send(clientSocket, message, strlen(message), 0);
    
    fprintf(stdout, "Wrote: %s.\n", message);
    fflush(stdout);
    
    char buffer[BUFSIZE];
    size_t bytesTotal = 0;
    size_t bytesTotalOfFile = 0;
    
    while (1) {
        size_t bytesRead = recv(clientSocket, buffer, BUFSIZE, 0);
        bytesTotal = bytesTotal + bytesRead;
        gfr->bytesreceived = bytesTotal;
        
        if (bytesRead < 0) {
            return -1;
        }
        
        if (bytesRead == 0) {
            fprintf(stdout, "Connection stopped. Bytes total: %zu.\n", bytesTotal);
            fflush(stdout);
            char *scheme = strtok(fullData, " ");
            char *status = strtok(NULL, " ");
            char *filelength = strtok(NULL, " \r\n");
            
            if (strcmp(scheme, "GETFILE") != 0) {
                fprintf(stdout, "Invalid Scheme.\n");
                fflush(stdout);
                gfr->statusText = "INVALID";
                gfr->status = gfc_get_status(gfr);
                gfr->bytesOfFileReceived = bytesTotal;
                fprintf(stdout, "Invalid Scheme.\n");
                fflush(stdout);
                return -1;
            }
            
            if (strcmp(status, "OK") != 0) {
                fprintf(stdout, "Invalid Status.\n");
                fflush(stdout);
                gfr->statusText = "INVALID";
                gfr->status = gfc_get_status(gfr);
                gfr->bytesOfFileReceived = bytesTotal;
                
                return -1;
            }
            
            if (filelength != NULL) {
                
                fprintf(stdout, "Valid filelength.\n");
                fflush(stdout);
                gfr->filelengthstring = filelength;
                gfr->filelength = atol(filelength);
                
            }
            
            gfr->bytesOfFileReceived = bytesTotal;
            close(clientSocket);
            return -1;
        }
        
        fprintf(stdout, "All Bytes: %zu. Data: %s.\n", bytesTotal, buffer);
        fflush(stdout);
        
        if (headerComplete != 1) {
            
            fprintf(stdout, "Check 1.\n");
            fflush(stdout);
            
            char * finalData = (char *) malloc(bytesTotal + 1);
            strcpy(finalData, fullData);
            strcat(finalData, buffer);
            
            fullData = finalData;
            
            char * dataForAnalysis = (char *) malloc(strlen(finalData));
            strcpy(dataForAnalysis, finalData);
            
            if (bytesTotal > 7) {
                char *scheme = strtok(dataForAnalysis, " ");
                if (strcmp(scheme, "GETFILE") != 0) {
                    gfr->statusText = "INVALID";
                    gfr->status = gfc_get_status(gfr);
                    gfr->bytesOfFileReceived = bytesTotal;
                    return -1;
                }
                
                if (bytesTotal > 10) {
                    char *statusText = strtok(NULL, " \r\n");
                    if (strcmp(statusText, "OK") != 0) {
                        if (strcmp(statusText, "FILE_NOT_FOUND") == 0) {
                            gfr->statusText = statusText;
                            gfr->status = gfc_get_status(gfr);
                            if (gfr->headerargument != NULL) {
                                gfr->headerfunc("GETFILE FILE_NOT_FOUND", strlen("GETFILE FILE_NOT_FOUND"), gfr->headerargument);
                            }
                            return 0;
                        }
                        if (strcmp(statusText, "ERROR") == 0) {
                            gfr->statusText = statusText;
                            gfr->status = gfc_get_status(gfr);
                            if (gfr->headerargument != NULL) {
                                gfr->headerfunc("GETFILE ERROR", strlen("GETFILE ERROR"), gfr->headerargument);
                            }
                            return 0;
                        }
                    }
                    else {
                        fprintf(stdout, "Check 3.\n");
                        fflush(stdout);
                        
                        gfr->statusText = statusText;
                        gfr->status = gfc_get_status(gfr);
                        
                        char *everythingAfterStatus = strtok(NULL, "");
                        
                        char *filelength = strtok(everythingAfterStatus, " \r\n");
                        char *waste1 = strtok(NULL, "\n");
                        char *waste2 = strtok(NULL, "\n");
                        char *filecontent = strtok(NULL, "\n");
                        
                        fprintf(stdout, "Check 4.\n");
                        fflush(stdout);
                        
                        fprintf(stdout, "Variable Check! Filelength: %s. Waste1: %s. Waste2: %s.\n", filelength, waste1, waste2);
                        fflush(stdout);
                        
                        if (filelength != NULL && waste1 != NULL) {
                            gfr->filelengthstring = filelength;
                            gfr->filelength = atol(filelength);
                            
                            headerComplete = 1;
                        }
                        
                        if (waste2 != NULL && strcmp(waste2, "\r") != 0) {
                            filecontent = waste2;
                        }
                        
                        if (filecontent != NULL) {
                            size_t actualFileLengthTransmitted = strlen(filecontent);
                            bytesTotalOfFile = bytesTotalOfFile + actualFileLengthTransmitted;
                            gfr->bytesOfFileReceived = bytesTotalOfFile;
                            
                            char *header = (char *) malloc(11 + strlen(filelength) + 6);
                            strcpy(header, "GETFILE OK ");
                            strcat(header, gfr->filelengthstring);
                            strcat(header, " \r\n\r\n");
                            
                            gfr->header = header;
                            
                            gfr->headerlength = strlen(header);
                            
                            if (gfr->headerargument != NULL) {
                                gfr->headerfunc(gfr->header, gfr->headerlength, gfr->headerargument);
                            }
                            
                            fprintf(stdout, "Bytes of File (actual): %zu. Bytes of File (theoretical): %zu.", bytesTotalOfFile, gfr->filelength);
                            fflush(stdout);
                            
                            if (bytesTotalOfFile == gfr->filelength) {
                                return 0;
                            }
                        }
                    }
                }
            }
        }
        else {
            fprintf(stdout, "Check 2.\n");
            fflush(stdout);
            
            bytesTotalOfFile = bytesTotalOfFile + bytesRead;
            gfr->writerfunc(buffer, bytesRead, gfr->writerargument);
            gfr->bytesOfFileReceived = bytesTotalOfFile;
            fprintf(stdout, "Bytes of File (actual): %zu. Bytes of File (theoretical): %zu.\n", bytesTotalOfFile, gfr->filelength);
            fflush(stdout);
            if (bytesTotalOfFile == gfr->filelength) {
                return 0;
            }
        }
    }
}

gfstatus_t gfc_get_status(gfcrequest_t *gfr){
    gfstatus_t status;
    int result;
    
    if ((result = strcmp(gfr->statusText, "OK")) == 0) {
        status = GF_OK;
    }
    else if ((result = strcmp(gfr->statusText, "FILE_NOT_FOUND")) == 0) {
        status = GF_FILE_NOT_FOUND;
    }
    else if ((result = strcmp(gfr->statusText, "ERROR")) == 0) {
        status = GF_ERROR;
    }
    else {
        status = GF_INVALID;
    }
    
    return status;
}

char* gfc_strstatus(gfstatus_t status){
    char* strstatus;
    
    if (status == GF_OK) {
        strstatus = "OK";
    }
    else if (status == GF_FILE_NOT_FOUND) {
        strstatus = "FILE_NOT_FOUND";
    }
    else if (status == GF_ERROR) {
        strstatus = "ERROR";
    }
    else {
        strstatus = "INVALID";
    }
    
    return strstatus;
}

size_t gfc_get_filelen(gfcrequest_t *gfr){
    return gfr->filelength;
}

size_t gfc_get_bytesreceived(gfcrequest_t *gfr){
    fprintf(stdout, "Bytes received returned: %zu.\n", gfr->bytesOfFileReceived);
    fflush(stdout);
    
    return gfr->bytesOfFileReceived;
}

void gfc_cleanup(gfcrequest_t *gfr){
}


void gfc_global_init(){
}

void gfc_global_cleanup(){
}