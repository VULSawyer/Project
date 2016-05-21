/* Modified original for the project 
*  by changing IP address, altering test data, adding linked list functions
*  removing unwanted generic functions and adding modbus functions*/
#include <stdio.h>

#include <libbacnet/address.h>			/*bacnet headers*/
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"

//is this all I need?
#include "modbus-tcp.h"				/*modbus headers*/
//#include "sys/socket.h"
//#include "sys/type.h"

#define BACNET_DEVICE_ID	52		//Is this correct??	
#define BACNET_INSTANCE_NO	12		//where did this go?
#define BACNET_PORT		0xBAC1
#define BACNET_INTERFACE	"lo"
#define BACNET_DATALINK_TYPE	"bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	    	/* msec */

//#define SERVER_PORT		502		// What is this?
#define SERVER_PORT		0xBAC0		/* supplied addresses*/
//#define SERVER_ADDRESS	"127.0.0.1"	// loopback address for testing
#define SERVER_ADDRESS		"140.159.153.159"    

#define RUN_AS_BBMD_CLIENT	1		/* default true so next part will run*/

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT	0xBAC1
#define BACNET_BBMD_ADDRESS	"127.0.0.1" 	/* test only- change to "140.159.160.7" */
#define BACNET_BBMD_TTL		90
#endif

//---------------------------------------------------------------------
/* SELF TESTING BACnet client will print "Successful match" whenever it is able to receive
 * this set of data (matching data in EES4100_Testbench/src/random_data /12) */
static uint16_t test_data[] = {
    0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C };
#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))

//---------------------------------------------------------------------
//have to do the following:

/*1. Linked list objects*/
/*typedef struct s_word_object word_object;
struct s_word_object {
	char *word;
	word_object *next;
	};			/*this is not a function, so ends with ;
static word_object *list_head;	This is copied from mylink_list.c and needs some changes*/

/*2. Share list using list lock*/
/*FROM thread1.c:
static word_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;
*/

/*3. Add objects to list*/
/*FROM mylink_list.c:
static void add_to_list(char *word) {
	word_object *last_object;
	if (list_head == NULL) {
		last_object = malloc(sizeof(word_object));
		list_head = last_object;
	}
	else{
		last_object = list_head;
		while (last_object->next) last_object = last_object->next;
		last_object->next = malloc(sizeof(word_object));
		last_object = last_object->next;
	}
	last_object->word = strdup(word);	//duplicate string command
	last_object->next = NULL;
}*/


/*4. Allocate memory to each object*/
//See above in 3


static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

//---------------------------------------------------------------------
//gathering the data from modbus server to send to Bacnet client
static int Update_Analog_Input_Read_Property(
		BACNET_READ_PROPERTY_DATA *rpdata) {

	static int index;
	int instance_no = bacnet_Analog_Input_Instance_To_Index(
		rpdata->object_instance);

	if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;

	printf("AI_Present_Value request for instance %i\n", instance_no);
	/* Update the values to be sent to the BACnet client here.
	* The data should be read from the head of a linked list. You are required
	* to implement this list functionality.
	* 	bacnet_Analog_Input_Present_Value_Set() 
	*	First argument: Instance No
	*	Second argument: data to be sent
	*
	* Without reconfiguring libbacnet, a maximum of 4 values may be sent */
	bacnet_Analog_Input_Present_Value_Set(0, test_data[index++]);
	/* bacnet_Analog_Input_Present_Value_Set(1, test_data[index++]); */
	/* bacnet_Analog_Input_Present_Value_Set(2, test_data[index++]); */
    
	if (index == NUM_TEST_DATA) index = 0;
		
	not_pv:		/*If not present value program comes here*/
	return bacnet_Analog_Input_Read_Property(rpdata);
}
//---------------------------------------------------------------------

static bacnet_object_functions_t server_objects[] = {
	{bacnet_OBJECT_DEVICE,
		NULL,
		bacnet_Device_Count,
		bacnet_Device_Index_To_Instance,
		bacnet_Device_Valid_Object_Instance_Number,
		bacnet_Device_Object_Name,
		bacnet_Device_Read_Property_Local,
		bacnet_Device_Write_Property_Local,
		bacnet_Device_Property_Lists,
		bacnet_DeviceGetRRInfo,
		NULL, /* Iterator */
		NULL, /* Value_Lists */
		NULL, /* COV */
		NULL, /* COV Clear */
		NULL  /* Intrinsic Reporting */},
	{bacnet_OBJECT_ANALOG_INPUT,
		bacnet_Analog_Input_Init,
		bacnet_Analog_Input_Count,
		bacnet_Analog_Input_Index_To_Instance,
		bacnet_Analog_Input_Valid_Instance,
		bacnet_Analog_Input_Object_Name,
		Update_Analog_Input_Read_Property,
		bacnet_Analog_Input_Write_Property,
		bacnet_Analog_Input_Property_Lists,
		NULL /* ReadRangeInfo */ ,
		NULL /* Iterator */ ,
		bacnet_Analog_Input_Encode_Value_List,
		bacnet_Analog_Input_Change_Of_Value,
		bacnet_Analog_Input_Change_Of_Value_Clear,
		bacnet_Analog_Input_Intrinsic_Reporting},
	{MAX_BACNET_OBJECT_TYPE}
};
//---------------------------------------------------------------------

/*Running as BBMD client*/
static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(
	    bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS), 
	    htons(BACNET_BBMD_PORT),
	    BACNET_BBMD_TTL);
#endif
}	
//---------------------------------------------------------------------

/*Setting up minute and second timers*/
static void *minute_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/*Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */
	
	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}

static void *second_tick(void *arg) {
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions and ack
	 * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/*removed more bacnet functions not used here*/
	
	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}
//---------------------------------------------------------------------
//MODBUS THREAD IN HERE

static void *modbus_start(void *arg)		//allocate and initialize a structure
{
	uint16_t tab_reg[32];
	int reg_num;				//register number
	int inst;				//instance qty
	modbus_t *ctx;
	int rc, i;

	restart:
	ctx = modbus_new_tcp(SERVER_ADDRESS, SERVER_PORT);//context is modbus server address
	

	if (ctx==NULL)	{
		fprintf(stderr, "Unsuccessful allocation and initialization\n");	
		sleep(1);
		goto restart;
	}
	if (modbus_connect(ctx) == -1)	{	//establish connection
		fprintf(stderr, "Unsuccessful connection to server: %s\n",modbus_strerror(errno));
		modbus_free(ctx);
		modbus_close(ctx);
		sleep(1);
		goto restart;
	}
	else {
		fprintf(stderr, "Successful connection to server\n");
	}

//read modbus registers	

	{
	rc = modbus_read_registers(ctx, 44, 2, tab_reg);	//assigned addressed
	if (rc == -1)	{
		fprintf(stderr, "Register read failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		modbus_close(ctx);
		goto restart;
	}
		for (i = 0; i < rc; i++) {
			//add_to_list(&list_head[i], tab_reg[i]);
			printf("Register[%d] = [%d] (0x%X)\n", i, tab_reg[i], tab_reg[i]);

		}
		sleep(0.1);		//100ms sleep
		return 0;
	}
}
//end of modbus thread
//---------------------------------------------------------------------

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)
//---------------------------------------------------------------------
//MAIN -needs modbus thread stuff added

int main(int argc, char **argv) {
	uint8_t rx_buf[bacnet_MAX_MPDU];
	uint16_t pdu_len;
	BACNET_ADDRESS src;
	pthread_t minute_tick_id, second_tick_id;
	pthread_t modbus_start_id;			/*added modbus thread*/
	bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
	bacnet_address_init();

/* Setup device objects */
	bacnet_Device_Init(server_objects);
	BN_UNC(WHO_IS, who_is);
	BN_CON(READ_PROPERTY, read_property);

	bacnet_BIP_Debug = true;
	bacnet_bip_set_port(htons(BACNET_PORT));
	bacnet_datalink_set(BACNET_DATALINK_TYPE);
	bacnet_datalink_init(BACNET_INTERFACE);
	atexit(bacnet_datalink_cleanup);
	memset(&src, 0, sizeof(src));

	register_with_bbmd();

	bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

	pthread_create(&minute_tick_id, 0, minute_tick, NULL);
	pthread_create(&second_tick_id, 0, second_tick, NULL);
//Thread 3 added
	pthread_create(&modbus_start_id, 0, modbus_start, NULL);//where does this come from?
    

	/* Start another thread here to retrieve your allocated registers from the
     * modbus server. This thread should have the following structure (in a
     * separate function):
     *
     * Initialise:
     *	    Connect to the modbus server
     *
     * Loop:
     *	    Read the required number of registers from the modbus server
     *	    Store the register data into the tail of a linked list 
     */
//---------------------------------------------------------------------
/*holding the lock to guarantee atomicity*/

	while (1) {
		pdu_len = bacnet_datalink_receive(
		    &src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

		if (pdu_len) {
	    /* May call any registered handler.
	     * Thread safety: May block, however we still need to guarantee
	     * atomicity with the timers, so hold the lock anyway */
			pthread_mutex_lock(&timer_lock);
			bacnet_npdu_handler(&src, rx_buf, pdu_len);
			pthread_mutex_unlock(&timer_lock);
		}

		//ms_tick();
	}

	return 0;
}
//END OF PROGRAM
