#define USE_STDPERIPH_DRIVER
#include "stm32f10x.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
//#include <string.h>
#include <stddef.h>

/* Filesystem includes */
#include "filesystem.h"
#include "fio.h"

/* variable parameter function*/
#include <stdarg.h>

extern const char _sromfs;

static void setup_hardware();

volatile xSemaphoreHandle serial_tx_wait_sem = NULL;
volatile xQueueHandle serial_str_queue = NULL;
volatile xQueueHandle serial_rx_queue = NULL;

/* Queue structure used for passing messages. */
typedef struct {
	char str[100];
} serial_str_msg;

/* Queue structure used for passing characters. */
typedef struct {
	char ch;
} serial_ch_msg;


/*simple printf, support int, char and string*/
void MYprintf(const char *format, ...){
	va_list ap;
	va_start(ap, format);
	int curr_ch = 0;
	char out_ch[2] = {'\0', '\0'};
	char percentage[] = "%";
	char *str;
	char str_num[10];
	int out_int;

	while( format[curr_ch] != '\0' ){
		if(format[curr_ch] == '%'){
			if(format[curr_ch + 1] == 's'){
				str = va_arg(ap, char *);
        	    while (!xQueueSendToBack(serial_str_queue, str, portMAX_DELAY)); 
        	    //parameter(...,The address of a pointer that point to the string which been put in queue,...)
			}else if(format[curr_ch + 1] == 'd'){
				itoa(va_arg(ap, int), str_num);
        	    while (!xQueueSendToBack(serial_str_queue, str_num, portMAX_DELAY)); 
            }else if(format[curr_ch + 1] == 'c'){
                out_ch[0] = (char)va_arg(ap, int);
        	    while (!xQueueSendToBack(serial_str_queue, out_ch, portMAX_DELAY)); 
           }else if(format[curr_ch + 1] == 'x'){
                xtoa(va_arg(ap, int), str_num);
        	    while (!xQueueSendToBack(serial_str_queue, str_num, portMAX_DELAY)); 
			}else if(format[curr_ch + 1] == '%'){
        	    while (!xQueueSendToBack(serial_str_queue, percentage, portMAX_DELAY)); 
			}
			curr_ch++;
		}else{
		    out_ch[0] = format[curr_ch];
    	    while (!xQueueSendToBack(serial_str_queue, out_ch, portMAX_DELAY));
		}
		curr_ch++;
	}//End of while
	va_end(ap);
}


/* IRQ handler to handle USART2 interruptss (both transmit and receive
 * interrupts). */
void USART2_IRQHandler()
{
	static signed portBASE_TYPE xHigherPriorityTaskWoken;
	serial_ch_msg rx_msg;

	/* If this interrupt is for a transmit... */
	if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET) {
		/* "give" the serial_tx_wait_sem semaphore to notfiy processes
		 * that the buffer has a spot free for the next byte.
		 */
		xSemaphoreGiveFromISR(serial_tx_wait_sem, &xHigherPriorityTaskWoken);

		/* Diables the transmit interrupt. */
		USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
		/* If this interrupt is for a receive... */
	}
	else if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
		/* Receive the byte from the buffer. */
		rx_msg.ch = USART_ReceiveData(USART2);

		/* Queue the received byte. */
		if(!xQueueSendToBackFromISR(serial_rx_queue, &rx_msg, &xHigherPriorityTaskWoken)) {
			/* If there was an error queueing the received byte,
			 * freeze. */
			while(1);
		}
	}
	else {
		/* Only transmit and receive interrupts should be enabled.
		 * If this is another type of interrupt, freeze.
		 */
		while(1);
	}

	if (xHigherPriorityTaskWoken) {
		taskYIELD();
	}
}

void read_romfs_task(void *pvParameters)
{
	char buf[128];
	size_t count;
	int fd = fs_open("/romfs/test.txt", 0, O_RDONLY);
	do {
		//Read from /romfs/test.txt to buffer
		count = fio_read(fd, buf, sizeof(buf));
		
		//Write buffer to fd 1 (stdout, through uart)
		fio_write(1, buf, count);
	} while (count);
	
	while (1);
}

void send_byte(char ch)
{
	/* Wait until the RS232 port can receive another byte (this semaphore
	 * is "given" by the RS232 port interrupt when the buffer has room for
	 * another byte.
	 */
	while (!xSemaphoreTake(serial_tx_wait_sem, portMAX_DELAY));

	/* Send the byte and enable the transmit interrupt (it is disabled by
	 * the interrupt).
	 */
	USART_SendData(USART2, ch);
	USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

char receive_byte()
{
	serial_ch_msg msg;

	/* Wait for a byte to be queued by the receive interrupts handler. */
	while (!xQueueReceive(serial_rx_queue, &msg, portMAX_DELAY));

	return msg.ch;
}

void rs232_Tx_msg_task(void *pvParameters)
{
	serial_str_msg msg;
	int curr_char;

	while (1) {
		/* Read from the queue.  Keep trying until a message is
		 * received.  This will block for a period of time (specified
		 * by portMAX_DELAY). */
		while (!xQueueReceive(serial_str_queue, &msg, portMAX_DELAY));

		/* Write each character of the message to the RS232 port. */
		curr_char = 0;
		while (msg.str[curr_char] != '\0') {
			send_byte(msg.str[curr_char]);
			curr_char++;
		}
	}
}

#define NUM_CMD 6
void echo_str(int curr_char, char *str);
void show_ps(int curr_char, char *str);
void show_hello(int curr_char, char *str);
void show_heap_size(int curr_char, char *str);
void show_help(int curr_char, char *str);
void alco_mem(int curr_char, char *str);

typedef struct{
    char *name;       //command name
    char *descri;     //describe the command 
    void (*funt)(int curr_char, char *str);       //point to where the command function is
}command;

command cmd[NUM_CMD] = {   {"echo", "echo [string], return the string.", echo_str}, 
                           {"ps", "print the all task and its information." , show_ps},
                           {"hello", "Hello World!!", show_hello},
                           {"meminfo", "print max and free heap size" , show_heap_size},
                           {"help", "Where you are", show_help},      
                           {"memtest", "memtest [int], allocate memory" , alco_mem}
                       };

/*function of every command*/
void echo_str(int curr_char, char *str){
    //remove the "echo " in the str[]
    curr_char = 5;
    while(str[curr_char] != '\0'){
        str[curr_char - 5] = str[curr_char];
        curr_char++;
    }//End of while
    str[curr_char - 5] = '\0';
    MYprintf("%s", str);
}

void alco_mem(int curr_char, char *str){
    int num;
    char *s, *s1;
    //remove the "memtest " in the str[]
    curr_char = 8;
    while(str[curr_char] != '\0'){
        str[curr_char - 8] = str[curr_char];
        curr_char++;
    }//End of while
    str[curr_char - 8] = '\0';
    num = atoi(str);
    s = (char*) pvPortMalloc(sizeof(char) * num);
    s1 = (char*) pvPortMalloc(sizeof(char));
    MYprintf("Allocate %d byte\n\r", num);
    MYprintf("allocated address from \n\r%x\n\r", s);
    MYprintf("to \n\r%x\n\r", s1);
    //vPortFree(s);
    //vPortFree(s1);
}

void show_ps(int curr_char, char *str){
    portCHAR buf[100];    
    vTaskList(buf);
    MYprintf("Name\t\t\tState  Priority Stack  Num");
    MYprintf("%s", buf);       
}

void show_hello(int curr_char, char *str){
    MYprintf("Hello World!!!!");
}

void show_heap_size(int curr_char, char *str){
    //heap size
    MYprintf("Maximun size: %d (%x) byte\n\r", configTOTAL_HEAP_SIZE, configTOTAL_HEAP_SIZE);
    MYprintf("Free Heap Size: %d (%x) byte\n\r", xPortGetFreeHeapSize(), xPortGetFreeHeapSize());	 
}

void show_help(int curr_char, char *str){
    int i = -1;    
    while(i++ < NUM_CMD-1)
        MYprintf("%s\t\t%s \n\r", cmd[i].name, cmd[i].descri);
}

typedef enum{
        NONE,       //default
        ECHO ,      //echo the input char
        ENTER,      //type enter
        BACKSPACE,  //type backspace
}key_type;


void shell_task(void *pvParameters)
{
    serial_str_msg msg;
    char *str;
    char cmd_str[10];
    char data_str[100];
    char newLine[3] = {'\r', '\n', '\0'};
    char backspace[4] = {'\b', ' ', '\b', '\0'};
    char noCMD[] = "Command not found\0";
    char MCU[] = "stm32";
    char user[] = "pJay";
    char ps_title[] = "PID\tstatus\t\tpriority\n\r";
    char ch;
    int curr_char, i, done;

    key_type key;
    
    MYprintf("%d\n\r",sizeof(int));
    str = (char*) pvPortMalloc(sizeof(char)*20000);
    MYprintf("%x\n\r",str);
    str = (char*) pvPortMalloc(sizeof(char));
    MYprintf("%x\n\r",str);
    str = (char*) pvPortMalloc(sizeof(int));
    MYprintf("%x\n\r",str);
    str = (char*) pvPortMalloc(sizeof(int));
    MYprintf("%x\n\r",str);        

    while (1) {
        curr_char = 0;
        done = 0;
        str[0] = '\0';
       
        MYprintf("%s @ %s :$ ", user, MCU);
        
        do {
            // Receive a byte from the RS232 port            
            ch = receive_byte();

            // Checking input char                                     
            if (curr_char >= 98 || (ch == '\r') || (ch == '\n')) {
                key = ENTER;	
            }
            else if(ch != 127){
                key = ECHO;
            }else if(ch == 127 && curr_char > 0){
                key = BACKSPACE;
            }else key = NONE;
                        
            switch(key){
                case ECHO:
                    str[curr_char++] = ch;
                    MYprintf("%c", ch);
                    break;
                case BACKSPACE:
                    MYprintf("%s", backspace);
                    curr_char--;
                    str[curr_char] = '\0';
                    break;
                case ENTER:
                    str[curr_char] = '\0';
                    MYprintf("%s", newLine);
                    done = -1;	
                    break;
                    default:;
            }//End of switch

        } while (!done);

        //------ check cmd -------
        for(i = 0; i < NUM_CMD + 1 ; i++){
            if( i == NUM_CMD ){
                MYprintf("%s", noCMD);
                break;
            }else if(!strncmp(str, cmd[i].name, strlen(cmd[i].name) ) ){
                cmd[i].funt(curr_char, str);
                break;
            }
        }
        vPortFree(str);           
        MYprintf("%s", newLine);
    }//End of while
}//End of shell_task(void *pvParameters)


int main()
{
	init_rs232();
	enable_rs232_interrupts();
	enable_rs232();
	
	fs_init();
	fio_init();

	/* Create the queue used by the serial task.  Messages for write to
	 * the RS232. */
	serial_str_queue = xQueueCreate(10, sizeof(serial_str_msg));
	vSemaphoreCreateBinary(serial_tx_wait_sem);
	serial_rx_queue = xQueueCreate(1, sizeof(serial_ch_msg));

#if 0
	register_romfs("romfs", &_sromfs);
	/* Create a task to output text read from romfs. */
	xTaskCreate(read_romfs_task,
	            (signed portCHAR *) "Read romfs",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY + 2, NULL);
#endif

    /* Create a task to write messages from the queue to the RS232 port. */
	xTaskCreate(rs232_Tx_msg_task,
	            (signed portCHAR *) "Tx Str from queue",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY + 2, NULL);

	xTaskCreate(shell_task,
	            (signed portCHAR *) "shell task",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY + 10, NULL);

	/* Start running the tasks. */
	vTaskStartScheduler();

	return 0;
}

void vApplicationTickHook()
{
}
