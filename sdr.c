#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <ctype.h>


#define RADIO_PERIPH_ADDRESS 0x43c00000
#define FIFO_ADDRESS         0x43c10000

#define PORT	    25344 // destination port
#define MAX_PACKET  512   // 512 8-bit values (1024 bytes)



uint32_t adc = 30000000;  // ADC frequency (Hz)
uint32_t tune= 30001000;  // Tune frequency (Hz)
uint8_t udpstream, playback;  // play, stream indicators
char ip[20];      // IP address to send packets to
int mem_fd;    // memory object
int qwit = 0; // exit the program


void disp_menu();
void get_input(uint32_t* pinc);
uint8_t radio_IF ();




// UDP Packet generation and streaming.  Runs in the background.
void *ethrnt (void *vargp)
{
	void *fifo_map_base = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, FIFO_ADDRESS);
	volatile int *fifo_base = (volatile int *)fifo_map_base;
	
	int sockfd;
	struct sockaddr_in	 servaddr;
	uint8_t pkt_count;
	uint8_t buffer[2*(MAX_PACKET+1)];	
	int indx = 0;
	size_t datalength = sizeof(buffer);
	uint32_t tdata;

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		return NULL;
	}
	memset(&servaddr, 0, sizeof(servaddr));
		
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = inet_addr(ip);
	//printf ("dbg: servaddr filled out\n");
	while (1)
	{
		if (udpstream)
		{	
			memset((buffer+1), 0, MAX_PACKET); // reset buffer
			if ((*fifo_base+1) >= MAX_PACKET )
			{
				for(indx = 0; indx < 256; indx ++)
				{   // theres data in fifo. read FIFO, append buffer
					tdata = *fifo_base;
					buffer[(indx*4)+1] = (uint8_t)(tdata);
					buffer[(indx*4)+2] = (uint8_t)(tdata>>8);
					buffer[(indx*4)+3] = (uint8_t)(tdata>>16);
					buffer[(indx*4)+4] = (uint8_t)(tdata>>24);
					
				}
  		  // transmit packet
  			buffer[0]++; //Increment counter
  			sendto(sockfd, buffer, datalength, MSG_CONFIRM, (const struct sockaddr*) &servaddr, sizeof(servaddr));
      }
			else 
			{  // no data in FIFO, wait 1 second and check again
					sleep(1);
					if ((*fifo_base+1)==0) 
					{ // radio is off, stop streaming
						indx = 256;
						udpstream = 0;
					} 
			}	
		}
		else if (qwit) {break;}
	}
	close(sockfd);
}




// main thread, configures radio, handles UI
void main()
{
	printf("\e[1;1H\e[2J"); // clear screen
	mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *radio_map_base = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, RADIO_PERIPH_ADDRESS);
	volatile int *radio_base = (volatile int*) radio_map_base;
	playback = 0;
	udpstream = 0;
	
	// 1st prompt user for destination IP address.
	while(1)
	{	
		int ip1, ip2, ip3, ip4;
		printf("Enter Destination IP Address: ");
		int p = scanf("%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);
		if (p>0)
		{
			if( (ip1>255) || (ip2>255) || (ip3>255) || (ip4>255) )
			{
				printf("\nInvalid IP, retry...\n");
			}
			else
			{
				sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
				break;
			}
		}
	}
	// create ethernet background thread
	pthread_t ethrnt_thrd;
	pthread_create(&ethrnt_thrd, NULL, ethrnt, NULL);

	// Main UI thread
	while(1)
	{
		if (radio_IF() > 0)
		{
			float tune_phase = (float)(1<<27)/125000000.*(float)tune;
			float adc_phase = (float)(1<<27)/125000000.*(float)adc;
			*radio_base = adc_phase;
			*(radio_base+1) = tune_phase;
			*(radio_base+2) = playback;
		}
		else if (qwit) 
		{
			*(radio_base+2) = 0;
			break;
		}
		sleep(1);
	}		
	sleep(1);	
}


/*-----------------------------------------------*/
// UI Handlers

// User input
uint8_t radio_IF ( )
{
		// Display user menu
		disp_menu();
		// wait for user input
		char usr_input = toupper(getchar());
		switch (usr_input)
		{
			case 'A':
				get_input(&adc);
				return 1;
			case 'T':
				get_input(&tune);
				return 1;
			case 'N':
				udpstream ^= 1<<0;
				return 0;
			case 'P':
				playback ^= 1<<0;
				return 1;
			case 'Q':
				qwit = 1;
				return 0;
			default:
				break;
		}
	return 0;
}


// For setting the frequency.  will not update if input is invalid
void get_input(uint32_t* pinc)
{
	uint32_t temp;
	
	printf("Enter desired frequency in Hz: ");
	if(scanf("%8u", &temp)) {*pinc = temp;}
	else {printf("invalid input.....\n");}
}


// Menu display...
void disp_menu()
{
	printf("\e[1;1H\e[2J");
	printf("SDR Project\nEmmanuel Coleman\n");
	printf("Controls:\n------------\n");
	printf("a/A -- Set ADC Frequency (in Hz)\n");
	printf("t/T -- Set tuning frequency (in Hz)\n");
	printf("n/N -- Toggle UDP packet streaming on/off\n");
	printf("p/P -- Toggle audio playback (start/stop DDS compilers)\n");
	printf("q/Q -- Quit\n--------------------------------------\n");
	printf("ADC Frequency: %.3f MHz\n", (float)(adc/1.0e6));
	printf("Tune Frequency: %.3f MHz\n", (float)(tune/1.0e6));
	printf("Playback ");
	if (playback){printf("on\n");}
	else {printf("off\n");}
	printf("UDP Streaming ");
	if (udpstream){printf("on\n");}
	else {printf("off\n");}
}


