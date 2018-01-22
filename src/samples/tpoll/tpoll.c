/*
 * poll serial data from ttySAK1 port /analyzing / execute scripts
 * 
 * @by amartol
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include "uart.h"
#include "tpoll.h"

//int err;

serial_t* tty;
//char dev[]={"/dev/ttyUSB0"};
char dev[]={"/dev/ttySAK1"}; //for production
int baud = 115200;
uint8_t rxdata[BUFF_SIZE];
//u8 crct[]={"123"};
pid_t pid = -1;
volatile sig_atomic_t sig = IDLE;



int main(void) {
bool B_mode_wifi = true;
struct tm 		str_time;
struct timeval 	now;

//signals regictration
struct sigaction act;
memset (&act, 0, sizeof(act));
act.sa_handler = &sig_hdl;
sigset_t   set; 
sigemptyset(&set);
sigaddset(&set, SIGUSR1);
sigaddset(&set, SIGCHLD);
act.sa_mask = set;	//blocked sig

if (sigaction(SIGUSR1, &act, NULL) < 0) {
	perror ("sigaction init USR1");
	exit(EXIT_FAILURE);
}
if (sigaction(SIGCHLD, &act, NULL) < 0) {
	perror ("sigaction init child_terminated");
	exit(EXIT_FAILURE);
}


//create serial device connection
tty = serial_create();
if(serial_connect(tty, dev, baud) < 0){
	perror("serial connect");
	tpoll_exit(-1);
}


for(;;){
	usleep(200000);
	int cnt;
	if(serial_available(tty)) {
		if((cnt = rxdata_processing(tty)) > 0) {
			//processing cmd_code
			//printf("command %c\n", rxdata[2]);
			switch (rxdata[2]) {
			int i;
			case CMD_START_RECORD:
					if (access("/dev/mmcblk0", R_OK) == 0) {
						pid = fork();	//create new process
						if (pid != 0) {	//parent proc
							printf("child pid = %d\n", pid);					
						}
						else if (!pid) { //child proc
							serial_close(tty);
							serial_destroy(tty);
							if(execl("/usr/bin/akipcserver","akipcserver", NULL) < 0) {
								perror("child exec err");
								tpoll_exit(-4);
							}
						}
						else if (pid < 0) {
							perror ("fork process failed");
							send_responce (REC_ERROR);
						}
							
					else {
						printf("no SD card!\n");
						send_responce (REC_NO_SDCARD);
					}

					}

			break;
			
			case CMD_STOP_RECORD:
				//system("/etc/init.d/camera.sh stop");
				printf("stop record\n");
			break;
	
			case CMD_CLK_ADJUST:
				if (cnt == 0x0C){
				i = 3;
				str_time.tm_year = 	(int) rxdata[i];
				str_time.tm_mon = 	(int) rxdata[++i];
				str_time.tm_mday = 	(int) rxdata[++i];
				str_time.tm_hour = 	(int) rxdata[++i];
				str_time.tm_min = 	(int) rxdata[++i];
				str_time.tm_sec =	(int) rxdata[++i];
				str_time.tm_isdst = -1;
				//printf("%s\n", asctime(&str_time));
				now.tv_usec = 0;
				now.tv_sec = mktime(&str_time);
				
					if(settimeofday(&now, NULL) == 0) {
						printf("settimeofday() successful.\n");
					}
					else {
						perror("settimeofday() failed");
						//exit(EXIT_FAILURE);
					}
				}
				else printf("packet size for time incorrect.\n");
			break;
			
			case CMD_USB_HOST_MODE:
				if(B_mode_wifi == false) {
					system("/etc/init.d/udisk.sh stop");
					B_mode_wifi = true;
				}
			break;
			
			case CMD_USB_DEVICE_MODE:
				if(B_mode_wifi == true) {
					system("/etc/init.d/udisk.sh stop");
					B_mode_wifi = false;
				}
			break;			
			
			default:
			break;
			} //end switch
		} // end if processing
		else {								//error processing rxdata
			printf("clear rx buffer\n");
			serial_clear(tty);
		}	
	} //end serial available

	//signal received?
	if(sig != IDLE)	{
		int state_val = 0;
		switch (sig){
							
		case CHILD_EXIT:
			wait(&state_val);
			if (WIFEXITED(state_val))
				printf("Child exited with code %d\n", WEXITSTATUS(state_val));
			else if (WIFSIGNALED(state_val))
				printf("Child exited by signal %d\n", WTERMSIG(state_val));
			else printf("Child terminated abnormally\n");
			pid = -1;
		break;
		
		case MSG_USR1:
			printf("message 1 received\n");
		break;
		
		default:
		break;
		}
		sig = IDLE;
	}



} //end for

	
printf("exit from main loop\n");
if(pid != -1){
kill(pid,SIGTERM);
wait(NULL);
}
serial_close(tty);
serial_destroy(tty);
exit(EXIT_SUCCESS);
} 

/**
 * signal handler
 * @param signal - received signal
 */
void sig_hdl(int signal)
{
	switch (signal)
	{
		case SIGUSR1:
			sig = MSG_USR1;
		break;
		
		case SIGCHLD:
			sig = CHILD_EXIT;
		break;
		
		default:
		break;
	}
}

/**
 * Processing & analyze received bytes
 * 
 * 
 * @param *s - pointer to serial structure.
 * @return received bytes
 */
static int rxdata_processing (serial_t* s)
{
	int i;
	for(i = 0; i < BUFF_SIZE && serial_available(tty); i++){
			rxdata[i] = serial_get(s);
	}
	printf("%c %c bytes cnt=%d\n", rxdata[0], rxdata[i-1], i);
	
	//analyze received data
	if((rxdata[0] != STARTMSG) || (rxdata[i-1] != STOPMSG)) {
		printf("incorrect packet data: first byte 0x%X, last byte 0x%X\n", rxdata[0], rxdata[i-1]);
		//report to host
		return -1;
	}
	
	//analyze crc
	uint16_t crc, crc_rx;
	//printf("%d %d \n", rxdata[1], rxdata[1]-2);
	crc = gencrc(rxdata+1, rxdata[1] - 4); //calculate crc other than start/stop symbols & received crc
	
	crc_rx = rxdata[i-3] << 8 | rxdata[i-2];
	//printf("RECEIVED crc=0x%X\n", crc_rx);
	//printf("CALCULATED crc=0x%X\n",crc);
	if(crc != crc_rx) {
		printf("incorrect packet crc: received 0x%X, calculated 0x%X\n", crc_rx, crc);
		return -2;
	}
//printf("%c %c i=%d\n", rxdata[0], rxdata[i-1], i);
return (i-1);
}


/**
 * Generate  CRC-16/XMODEM
 * poly=0x1021, initial value=0x0000
 * @param *bfr - pointer to input data buffer.
 * @param len - bytes qty 
 * @return calculated value
 */
static uint16_t gencrc(uint8_t *bfr, size_t len)
{
uint16_t crc = 0x0;
    while(len--)
        crc = (crc << 8) ^ crctbl[(crc >> 8)^*bfr++];
    return crc;
}


/**
 * Send responce to MCU
 * @param cmd - command to transmit.
 * @return transmitted bytes 
 */
 
static int send_responce (uint8_t cmd) 
{
	uint8_t txbuf[32];
	uint16_t crc;
	uint8_t i = 0;
	txbuf[i]	= STARTMSG;
	//----------------------//
	txbuf[++i]	= 0x06; //qty tx bytes
	txbuf[++i] 	= cmd; 
	crc = gencrc(txbuf+1, (size_t) txbuf[1] - 2);
	txbuf[++i]	= crc >> 8; //hi
	txbuf[++i]	= (uint8_t)crc; //low
	//---------------------//
	txbuf[++i] 	= STOPMSG;
	int j = 0;
	for(j = 0; j <= i; j++)
		serial_put(tty, txbuf[j]);
return (j-1);
}

/**
 * Exit from the func
 * @param sigexit - signal to send parent process
 */
static void tpoll_exit(int sigexit)
{
if(pid != -1){
kill(pid,SIGTERM);
wait(NULL);
}
serial_close(tty);
serial_destroy(tty);
exit(sigexit);
}

