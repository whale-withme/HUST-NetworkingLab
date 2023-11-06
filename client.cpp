// Created by 应祚成 on 2023/10/24.
/*HUST-CSE 2023计网实验
 *基于UDP实现一个TFTP客户端，实现与服务器进行文件的上传与下载功能
 *使用clumsy工具，相关设置如下。
延迟(Lag)，把数据包缓存一段时间后再发出，这样能够模拟网络延迟的状况。
掉包(Drop)，随机丢弃一些数据。
节流(Throttle)，把一小段时间内的数据拦截下来后再在之后的同一时间一同发出去。
重发(Duplicate)，随机复制一些数据并与其本身一同发送。
乱序(Out of order)，打乱数据包发送的顺序。
篡改(Tamper)，随机修改小部分的包裹内容。
 */
//

#include <winsock2.h>
#include "iostream"
#include "stdio.h"
#include <cstdlib>
#include "time.h"
#include <string.h>
#include <stddef.h>
#include <chrono>

SOCKET getUdpSocket(){
    /*初始化socket库*/
    WSADATA wsadata;
    int err = WSAStartup(0x1010, &wsadata);
    if(err != 0){
        return -1;
    }

    /*创建socket*/
    SOCKET udpsocket = socket(AF_INET, SOCK_DGRAM, 0);     /*参数：协议族ipv4，数据报格式*/
    if(udpsocket == INVALID_SOCKET){
        printf("set up socket failed\n");
        WSACleanup();
        return -1;
    }
    return udpsocket;
}

/*构造sockaddr_in数据结构*/
sockaddr_in getaddr(char* ip, int port){
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);        /*将端口号地址转换成网络字节顺序*/
    addr.sin_addr.S_un.S_addr = inet_addr(ip);      /*点分十进制IP地址字符串转换为32位整数表示的IPv4地址*/
    return addr;
}

/*构造上传请求数据包*/
char* RequestUploadPack(char* name, int& datalen, int type){
    int name_len = strlen(name);
    /*buffer是WRQ*/
    char* buffer = new char [2+name_len+type+2];
    buffer[0] = 0x0;
    buffer[1] = 0x02;       /*上传文件是写请求*/
    memcpy(buffer+2, name, name_len);
    memcpy(buffer+2+name_len, "\0", 1);
    if(type == 8)
        memcpy(buffer+2+name_len+1, "netascii", 8);
    else
        memcpy(buffer+2+name_len+1, "octet", 5);
    memcpy(buffer+2+name_len+1+type, "\0", 1);
    datalen = 2+name_len+1+1+type;       /*写入datalen长度*/
    return buffer;
}

/*写入当前时间*/
void PrintTime(FILE* fp){
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

    // 将时间点转换为时间戳（秒数）
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);

    // 使用标准库的ctime函数将时间戳转换为可读的日期和时间字符串
    char timeString[100];
    std::strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", std::localtime(&timestamp));

    // 打印当前时间字符串
    fprintf(fp, "current time:%s\n", timeString);
    return ;
}

char* MakeData(FILE* fp, short& block_number, int &datalen){
    char tmp[512];
    int sum = fread(tmp, 1, 512, fp);      /*读完返回sum为512*/
    if(!ferror(fp)){
        char* buf = new char[4+sum];
        buf[0] = 0x0;
        buf[1] = 0x3;
        block_number = htons(block_number);
        memcpy(buf+2, (short*)&block_number, 2);
        block_number = ntohs(block_number);
        memcpy(buf+4, tmp, sum);
        datalen = sum+4;
        return buf;
    }
    return NULL;
}

char* RequestDownloadPack(char* name, int& datalen, int type){
    int name_len = strlen(name);
    char* buff = new char[name_len+2+2+type];
    buff[0] = 0x00;
    buff[1] = 0x01;     /*RRQ包的op是1*/
    memcpy(buff+2, name, name_len);
    memcpy(buff+2+name_len, "\0", 1);
    if(type == 8)
        memcpy(buff+name_len+3, "netascii", type);
    else
        memcpy(buff+name_len+3, "octet", type);
    memcpy(buff+2+name_len+1+type, "\0", 1);
    datalen = 2+name_len+1+type+1;
    return buff;
}

char* AckPack(short& number){
    char* buff = new char[4];
    buff[0] = 0x00;
    buff[1] = 0x04;
    number = htons(number);
    memcpy(buff+2, &number, 2);
    number = ntohs(number);
    return buff;
}


int main(){
    FILE* fp = fopen("ClientLog.txt", "a");
    SOCKET sock = getUdpSocket();
    sockaddr_in addr;     /*存放ip和端口的结构体*/
    char resend_buffer[2048];       /*重发缓冲区*/
    int sendTimes;
    clock_t start, end;     /*记录时间*/
    int buflen;             /*缓冲区长度*/
    int Timekill, recvTimes;       /*记录recv_from数据重发次数*/
    /*超时设置*/
    int recvtimeout = 1000;     /*分别为接收、发出文件的超时限额*/
    int sendtimeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvtimeout, sizeof(int));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendtimeout, sizeof(int));

    while(1){
        /*客户端将要上传文件*/
        printf("====================Loading========================\n");
        printf("=====            这里是TFTP客户端               ===\n");
        printf("=====Function:                                  ===\n");
        printf("=====1.上传文件    2.下载文件     3.退出        ===\n");
        printf("===================================================\n"); 
        int choice;
        scanf("%d", &choice);
        if(choice == 1){
            /*对要输入的文件名、文件内容进行预处理*/
            char target[64] = "127.0.0.1";
            addr = getaddr(target, 69);        /*client发送的ip和端口号放在addr中*/
            printf("输入要上传的文件名：\n");
            char name[20];
            scanf("%s", &name);
            int type;
            printf("选择上传的方式：1、netascii 2、octet\n");
            scanf("%d", &type);
            if(type == 1)
                type = 8;
            else if(type == 2)
                type = 5;
            int datalen;

            /*下面是WRQ构造*/
            char* sendData = RequestUploadPack(name, datalen, type);
            memcpy(resend_buffer, sendData, datalen);

            /*开始发送数据包*/
            int ans = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
            buflen = datalen;
            start = clock();        /*计时开始*/
            sendTimes = 1;  /*发送次数*/

            while(ans != datalen){
                std::cout << "send WRQ failed, file name: " << name << std::endl;
                if(sendTimes <= 10){
                    ans = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                    sendTimes++;
                }
                else
                    break;
            }
            if(sendTimes > 10){
                fprintf(fp, "Upload file failed. Error: sendto function timeout.\n");
                continue;   /*此次传输失败*/
            }

            PrintTime(fp);         /*向日志中写入当前时间*/
            fprintf(fp, "send WRQ to file : %s\n上传方式：netascii\n", name);
			printf("send WRQ successfully.\n");

            delete[] sendData;
            FILE* uploadFile = fopen(name, "rb");

            if(uploadFile == NULL){
                std::cout << "file" << name << "opne failed or file doesn't exit." << std::endl;
                continue;
            }

            short block = 0;
            datalen = 0;
            int RST = 0;        /*记录重发*/
            int FullSize = 0;
            while(1){
                /*server端开启新的socket*/
                char recv_buf[1024];
                sockaddr_in server;     /*服务器新开端口供接下来的传输*/
                int len = sizeof(server);
                /*这里的ans接受的就是ACK报文*/
                ans = recvfrom(sock, recv_buf, 1024, 0, (sockaddr*)&server, &len);
                int recv_len = strlen(recv_buf);        //接收区字段的长度大小
                recvTimes = 1;

                /*不断重新发送直到接收到正确的ACK报文*/
                while(ans < 0){
                    printf("%d", recvTimes);
                    if(recvTimes > 10){        /*超过10次未接受到ACK即放弃*/
                        printf("no receive datagram get. transmission failed.\n");
                        PrintTime(fp);
                        fprintf(fp, "Upload file %s failed\n", name);
                        break;
                    }

                    int ans = sendto(sock, resend_buffer, buflen, 0, (sockaddr*)&addr, len);     /*重发*/
                    RST++;
                    std::cout << "resend last block" << std::endl;

                    sendTimes = 1;       /*处理sendto情况*/
                    while(ans != buflen){
                        std::cout << "resend last block failed :" << sendTimes << "times" << std::endl;
                        if(sendTimes <= 10){
                            ans = sendto(sock, resend_buffer, buflen, 0, (sockaddr*)&addr, len);
                            sendTimes++;
                        }
                        else
                            break;
                    }

                    ans = recvfrom(sock, recv_buf, 1024, 0, (sockaddr*)&server, &len);
                    if(ans > 0 )
                        break;
                    recvTimes++;       /*重发次数++*/
                }

                if(ans > 0){
                    short operation;
                    memcpy(&operation, recv_buf, 2);
                    operation = ntohs(operation);
                    if(operation == 4){      /*确认ack数据包*/
                        short blockNumber;
                        memcpy(&blockNumber, recv_buf+2, 2);
                        blockNumber = ntohs(blockNumber);

                        if(blockNumber == block){        /*收到正确的block应答*/
                            addr = server;
                            if(feof(uploadFile) && datalen != 512){
                                std::cout << "Congratulations!\nUpload finished." << std::endl;
                                end = clock();
                                double runningtime = static_cast<double>(end-start);
                                PrintTime(fp);                   
                                printf("Send block number:%d File size:%d bytes Resend times:%d Average transmission rate: %.2lf kb/s\n", blockNumber, FullSize, RST, FullSize/runningtime);
                                fprintf(fp, "Upload file %s finished, sent time:%.2lfms, size:%d Average transmission rate: %.2lf kb/s\n", name, runningtime, FullSize, FullSize/runningtime);
                                break;
                            }

                            block++;        /*构造下一个上传的data包*/
                            sendData = MakeData(uploadFile, block, datalen);
                            if(sendData == NULL){
                                std::cout << "file read mistake." << std::endl;
                                fprintf(fp, "file %s read mistake.", name);
                                break;
                            }
                            buflen = datalen;
                            FullSize += datalen-4;      /*记录发送数据的总大小*/
                            memcpy(resend_buffer, sendData, datalen);       /*本次的block放进重发区*/

                            int ans = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                            sendTimes = 1;
                            while(ans != datalen){
                                std::cout << "send block " << block << "failed" << std::endl;
                                if(sendTimes <= 10){
                                    ans = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                                    sendTimes++;
                                }
                                else
                                    break;
                            }
                            if(sendTimes > 10){
                                fprintf(fp, "Upload block %d failed.\n", blockNumber); 
                           }
                            std::cout << "pack = " << block << "sent successfully!" << std::endl;
                        }
                    }else if(operation == 5){                        
                      char errorcode[3], errorContent[512];
                      memcpy(errorcode, recv_buf+2, 2);
                      errorcode[2] = '\0';
                      for(int i = 0; *(recv_buf+4+i); i++)
                        memcpy(errorContent+i, recv_buf+4+i, 1);
                    printf("errorcode %s: %s\n", errorcode, errorContent);
                    fprintf(fp, "Upload block failed.\n");
                    }
                }
        
            }
            
        }
        if(choice == 2){        /*用户进行下载*/
            char target[64] = "127.0.0.1";
            addr = getaddr(target, 69);
            printf("请输入要下载的文件名称：\n");
            char name[100];
            int type;
            scanf("%s", &name);
            printf("请选择要下载的方式：1、netascii 2、octet\n");
            scanf("%d", &type);
            if(type == 1)
                type = 8;
            else if(type == 2)
                type = 5;

            int datalen = 0;
            char* senddata = RequestDownloadPack(name, datalen, type);      /*制作RRQ包*/
            buflen = datalen;
            memcpy(resend_buffer, senddata, datalen);
            recvTimes = 1;     /*后面的recv包用的计数*/

            int ans = sendto(sock, senddata, datalen, 0, (sockaddr*)&addr, sizeof(addr));
            start = clock();
            PrintTime(fp);
            fprintf(fp, "send RRQ to file %s\n", name);
            printf("send RRQ successfully.\n");
            sendTimes = 1;

            while(ans != datalen){
                std::cout << "send RRQ failed time:" << sendTimes <<std::endl;
                if(sendTimes <= 10){
                    ans = sendto(sock, senddata, datalen, 0, (sockaddr*)&addr, sizeof(addr));/*重发RRQ*/
                    sendTimes++;
                }
                else
                    break;
            }
            if(sendTimes > 10)
                continue;
		
            /*请求成功，清除上次的发送缓冲区*/
            delete []senddata;

            FILE* downloadFile = fopen(name, "wb");
            if(downloadFile == NULL){
                std::cout << "Download file" << name << "open failed" << std::endl;
                continue;
            }
            int want_recv = 1;
            int RST = 0;
            int Fullsize = 0;
            while(1){
                /*不断接收data报文*/
                char buf[1024];
                sockaddr_in server;
                int len = sizeof(server);
                /*这里的ans接受的就是data报文*/
                ans = recvfrom(sock, buf, 1024, 0, (sockaddr*)&server, &len);
				
                if(ans < 0){
                	int backInfo;
                    printf("Lost-resend times: %d\n", recvTimes);
                    if(recvTimes > 10){        /*超过10次未接受到ACK即放弃*/
                        printf("File %s download failed\n", name);
                        PrintTime(fp);
                        fprintf(fp, "Download file %s failed\n", name);
                        break;
                    }
                    backInfo = sendto(sock, resend_buffer, buflen, 0, (sockaddr*)&addr, len);     /*重发*/
                    RST++;
                    std::cout << "Data block lost, resend last ACK." << std::endl;

                    sendTimes = 1;       /*处理send情况*/
                    while(backInfo != buflen){
                        std::cout << "resend last ACK failed :" << sendTimes << "times" << std::endl;
                        if(sendTimes <= 10){
                            backInfo = sendto(sock, resend_buffer, buflen, 0, (sockaddr*)&addr, len);
                            sendTimes++;
                        }
                        else
                            break;
                    }
                    if(sendTimes > 10)
                        break;
                    recvTimes++;       /*重发次数++*/
                }
                else if(ans > 0){
                    short flag;
                    memcpy(&flag, buf, 2);
                    flag = ntohs(flag);
                    if(flag == 3){      /*服务器端发来data包*/
                        /*先处理发送ACK包*/
                        addr = server;
                        short number = 0;
                        memcpy(&number, buf+2, 2);
                        number = ntohs(number);
                        std::cout << "Package number: " << number << std::endl;
						
                        char* ack = AckPack(number);
                        int sendAck = sendto(sock, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
                        sendTimes = 1;
                        while(sendAck != 4){   /*发送失误*/
                            std::cout << "sent last ACK failed" << sendTimes << std::endl;
                            if(sendTimes <= 10){
                                sendAck = sendto(sock, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
                                sendTimes++;
                            }
                            else
                                break;
                        }
                        if(sendTimes > 10)
                            break; 
						std::cout << "Send package " << number << " ACK successfully." << std::endl;
						
                        /*接收正确的data包*/
                        if(want_recv == number){
                            buflen = 4;
                            recvTimes = 1;
                            memcpy(resend_buffer, &ack, 4);      /*超时发生时，要不断重传当前收到的上一个包的ack*/
                            fwrite(buf+4, ans-4, 1, downloadFile);        /*把收到的数据写进去*/
                            Fullsize += ans-4;

                            if(ans-4 >=0 && ans-4 < 512){       /*传输完毕*/
                                std::cout << "Congratulations!\nDownload finished." << std::endl;
                                end = clock();
                                double runningtime =static_cast<double>(end-start);
                                PrintTime(fp);
                                printf("transmission rate is: %.2lf kb/s\n", Fullsize/runningtime);
                                printf("File %s download finished. \nHere are details:\nsent blocks:%d running time:%.2lf resend times: %d. Fullsize: %d\n", name, number,runningtime, RST, Fullsize);
                                goto finish;
                            }
                            want_recv++;
                        }
                    }

                   	else if(flag == 5){      /*回复的是error包*/
                        short errorcode;
                        memcpy(&errorcode, buf+2, 2);
                        errorcode = ntohs(errorcode);
                        char str_error[1024];
                        int i =0;
                        for(i = 0; *(buf+4+i); i++)
                            memcpy(str_error+i, buf+4+i, 1);
                        str_error[i] = '\0';
                        std::cout << "error package:" << str_error << std::endl;
                        PrintTime(fp);
                        fprintf(fp, "error code: %d, error content:%s\n", errorcode, str_error);
                        break;
                    }
                }
            }
        finish:
            fclose(downloadFile);
        }
        if(choice == 0)
            break;
    }
    fclose(fp);
    int err = closesocket(sock);
    if(err){
        printf("socket close failed.\n");
        fprintf(fp, "socket 关闭失败\n");
    }
    return 0;
}